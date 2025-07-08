/*
 * Kernel-Userspace Communication Bridge for AI-OS
 * File: userspace/daemon/kernel_bridge.c
 * 
 * This component handles communication between the kernel module
 * and the userspace AI daemon for advanced interpretation.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <fcntl.h>
 #include <sys/ioctl.h>
 #include <sys/mman.h>
 #include <pthread.h>
 #include <errno.h>
 #include <signal.h>
 #include <sys/stat.h>
 #include <stdarg.h>
 #include "../ai_os_common.h"
 
 /* IOCTL definitions for kernel communication */
 #define AI_OS_MAGIC 'A'
 #define AI_OS_ENABLE _IO(AI_OS_MAGIC, 1)
 #define AI_OS_DISABLE _IO(AI_OS_MAGIC, 2)
 #define AI_OS_GET_STATUS _IOR(AI_OS_MAGIC, 3, struct ai_os_status)
 #define AI_OS_SET_CONFIG _IOW(AI_OS_MAGIC, 4, struct ai_os_config)
 #define AI_OS_GET_REQUEST _IOR(AI_OS_MAGIC, 5, struct ai_os_request)
 #define AI_OS_SEND_RESPONSE _IOW(AI_OS_MAGIC, 6, struct ai_os_response)
 
 /* Bridge state */
 static struct {
     int kernel_fd;
     pthread_t bridge_thread;
     int running;
     pthread_mutex_t request_mutex;
     int pending_requests;
 } bridge_state = {-1, 0, 0, PTHREAD_MUTEX_INITIALIZER, 0};
 
 /* External functions from AI daemon */
 extern int ollama_interpret_command(const char *natural_command, const char *context, 
                                    char *shell_command, size_t command_size);
 
 /* Logging utility */
static FILE *log_file = NULL;
static void kernel_bridge_log(const char *fmt, ...) {
    va_list args;
    if (!log_file) {
        log_file = fopen("/var/log/ai-os/kernel_bridge.log", "a");
        if (!log_file) log_file = stderr;
    }
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    fflush(log_file);
    va_end(args);
}
 
 /* Initialize kernel communication */
 int kernel_bridge_init(void) {
     /* Open kernel device */
     bridge_state.kernel_fd = open("/proc/ai_os", O_RDWR);
     if (bridge_state.kernel_fd < 0) {
         kernel_bridge_log("Kernel Bridge: Failed to open kernel interface: %s\n", strerror(errno));
         return -1;
     }
     
     kernel_bridge_log("Kernel Bridge: Initialized communication with kernel module\n");
     return 0;
 }
 
 /* Get status from kernel module */
 int kernel_bridge_get_status(struct ai_os_status *status) {
     if (bridge_state.kernel_fd < 0) {
         return -1;
     }
     
     /* Read status from proc file */
     char buffer[1024];
     ssize_t bytes_read;
     
     lseek(bridge_state.kernel_fd, 0, SEEK_SET);
     bytes_read = read(bridge_state.kernel_fd, buffer, sizeof(buffer) - 1);
     if (bytes_read < 0) {
         return -1;
     }
     
     buffer[bytes_read] = '\0';
     
     /* Parse the status information */
     memset(status, 0, sizeof(*status));
     
     char *line = strtok(buffer, "\n");
     while (line) {
         if (strstr(line, "Status: Enabled")) {
             status->enabled = 1;
         } else if (strstr(line, "Status: Disabled")) {
             status->enabled = 0;
         } else if (strstr(line, "Debug Mode: On")) {
             status->debug_mode = 1;
         } else if (strstr(line, "Active Contexts:")) {
             sscanf(line, "Active Contexts: %d", &status->active_contexts);
         } else if (strstr(line, "Active Requests:")) {
             sscanf(line, "Active Requests: %d", &status->active_requests);
         } else if (strstr(line, "Total Requests:")) {
             sscanf(line, "Total Requests: %llu", &status->total_requests);
         } else if (strstr(line, "Successful Interpretations:")) {
             sscanf(line, "Successful Interpretations: %llu", &status->successful_interpretations);
         } else if (strstr(line, "Failed Interpretations:")) {
             sscanf(line, "Failed Interpretations: %llu", &status->failed_interpretations);
         } else if (strstr(line, "Blocked Commands:")) {
             sscanf(line, "Blocked Commands: %llu", &status->blocked_commands);
         }
         line = strtok(NULL, "\n");
     }
     
     return 0;
 }
 
 /* Enable/disable kernel module */
 int kernel_bridge_set_enabled(int enabled) {
     if (bridge_state.kernel_fd < 0) {
         kernel_bridge_log("Kernel Bridge: set_enabled called with invalid fd\n");
         return -1;
     }
     
     const char *cmd = enabled ? "enable" : "disable";
     ssize_t bytes_written = write(bridge_state.kernel_fd, cmd, strlen(cmd));
     
     if (bytes_written < 0) {
         kernel_bridge_log("Kernel Bridge: Failed to %s module: %s\n", 
                 cmd, strerror(errno));
         return -1;
     }
     
     kernel_bridge_log("Kernel Bridge: Module %s\n", enabled ? "enabled" : "disabled");
     return 0;
 }
 
 /* Set debug mode */
 int kernel_bridge_set_debug(int debug_on) {
     if (bridge_state.kernel_fd < 0) {
         kernel_bridge_log("Kernel Bridge: set_debug called with invalid fd\n");
         return -1;
     }
     
     const char *cmd = debug_on ? "debug_on" : "debug_off";
     ssize_t bytes_written = write(bridge_state.kernel_fd, cmd, strlen(cmd));
     
     if (bytes_written < 0) {
         kernel_bridge_log("Kernel Bridge: Failed to set debug mode: %s\n", 
                 strerror(errno));
         return -1;
     }
     
     kernel_bridge_log("Kernel Bridge: Debug mode %s\n", debug_on ? "enabled" : "disabled");
     return 0;
 }
 
 /* Process kernel interpretation request */
 static int process_kernel_request(const struct ai_os_request *request, 
                                  struct ai_os_response *response) {
     char interpreted_command[1024];
     int result;
     
     kernel_bridge_log("Kernel Bridge: Processing request %d from PID %d: %s\n", 
            request->request_id, request->pid, request->command);
     
     /* Initialize response */
     response->request_id = request->request_id;
     response->result_code = -1;
     response->interpreted_command[0] = '\0';
     response->error_message[0] = '\0';
     
     /* Use Ollama to interpret the command */
     result = ollama_interpret_command(request->command, request->context, 
                                     interpreted_command, sizeof(interpreted_command));
     
     if (result == 0) {
         response->result_code = 0;
         strncpy(response->interpreted_command, interpreted_command, 
                 sizeof(response->interpreted_command) - 1);
         response->interpreted_command[sizeof(response->interpreted_command) - 1] = '\0';
         
         kernel_bridge_log("Kernel Bridge: Successfully interpreted: %s -> %s\n", 
                request->command, interpreted_command);
     } else if (result == -2) {
         response->result_code = -2;
         strncpy(response->error_message, "Command marked as unsafe", 
                 sizeof(response->error_message) - 1);
     } else if (result == -3) {
         response->result_code = -3;
         strncpy(response->error_message, "Command unclear", 
                 sizeof(response->error_message) - 1);
     } else {
         response->result_code = -1;
         strncpy(response->error_message, "Interpretation failed", 
                 sizeof(response->error_message) - 1);
     }
     
     return 0;
 }
 
 /* Bridge thread function */
 static void *bridge_thread_func(void *arg) {
     struct ai_os_request request;
     struct ai_os_response response;
     fd_set readfds;
     struct timeval timeout;
     int result;
     
     kernel_bridge_log("Kernel Bridge: Bridge thread started\n");
     
     while (bridge_state.running) {
         /* Check for incoming requests from kernel */
         FD_ZERO(&readfds);
         FD_SET(bridge_state.kernel_fd, &readfds);
         
         timeout.tv_sec = 1;
         timeout.tv_usec = 0;
         
         result = select(bridge_state.kernel_fd + 1, &readfds, NULL, NULL, &timeout);
         
         if (result < 0) {
             if (errno == EINTR) continue;
             kernel_bridge_log("Kernel Bridge: Select error: %s\n", strerror(errno));
             break;
         }
         
         if (result == 0) {
             /* Timeout - continue loop */
             continue;
         }
         
         if (FD_ISSET(bridge_state.kernel_fd, &readfds)) {
             /* Read request from kernel */
             /* Note: This is simplified - real implementation would need
              * proper protocol for kernel-userspace communication */
             
             pthread_mutex_lock(&bridge_state.request_mutex);
             bridge_state.pending_requests++;
             pthread_mutex_unlock(&bridge_state.request_mutex);
             
             /* Simulate processing a request */
             usleep(100000); /* 100ms processing time */
             
             pthread_mutex_lock(&bridge_state.request_mutex);
             bridge_state.pending_requests--;
             pthread_mutex_unlock(&bridge_state.request_mutex);
         }
     }
     
     kernel_bridge_log("Kernel Bridge: Bridge thread terminated\n");
     return NULL;
 }
 
 /* Start the kernel bridge */
 int kernel_bridge_start(void) {
     if (bridge_state.kernel_fd < 0) {
         kernel_bridge_log("Kernel Bridge: Not initialized\n");
         return -1;
     }
     
     bridge_state.running = 1;
     
     if (pthread_create(&bridge_state.bridge_thread, NULL, bridge_thread_func, NULL) != 0) {
         kernel_bridge_log("Kernel Bridge: Failed to create bridge thread: %s\n", 
                 strerror(errno));
         bridge_state.running = 0;
         return -1;
     }
     
     kernel_bridge_log("Kernel Bridge: Started successfully\n");
     return 0;
 }
 
 /* Stop the kernel bridge */
 void kernel_bridge_stop(void) {
     if (bridge_state.running) {
         bridge_state.running = 0;
         
         if (bridge_state.bridge_thread) {
             if (pthread_join(bridge_state.bridge_thread, NULL) != 0) {
                 kernel_bridge_log("Kernel Bridge: Failed to join bridge thread: %s\n", strerror(errno));
             }
             bridge_state.bridge_thread = 0;
         }
         
         kernel_bridge_log("Kernel Bridge: Stopped\n");
     }
 }
 
 /* Cleanup kernel bridge */
 void kernel_bridge_cleanup(void) {
     kernel_bridge_stop();
     
     if (bridge_state.kernel_fd >= 0) {
         if (close(bridge_state.kernel_fd) != 0) {
             kernel_bridge_log("Kernel Bridge: Failed to close kernel fd: %s\n", strerror(errno));
         }
         bridge_state.kernel_fd = -1;
     }
     if (log_file && log_file != stderr) fclose(log_file);
     log_file = NULL;
     kernel_bridge_log("Kernel Bridge: Cleaned up\n");
 }
 
 /* Get pending request count */
 int kernel_bridge_get_pending_requests(void) {
     int count;
     pthread_mutex_lock(&bridge_state.request_mutex);
     count = bridge_state.pending_requests;
     pthread_mutex_unlock(&bridge_state.request_mutex);
     return count;
 }
 
 /* Advanced kernel communication using netlink sockets */
 #include <linux/netlink.h>
 #include <sys/socket.h>
 
 #define NETLINK_AI_OS 31
 #define AI_OS_MSG_INTERPRET 1
 #define AI_OS_MSG_RESPONSE 2
 
 struct ai_netlink_msg {
     struct nlmsghdr nlh;
     int msg_type;
     int request_id;
     pid_t pid;
     char data[1024];
 };
 
 static int netlink_fd = -1;
 
 /* Initialize netlink communication */
 int kernel_bridge_init_netlink(void) {
     struct sockaddr_nl src_addr;
     
     /* Create netlink socket */
     netlink_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_AI_OS);
     if (netlink_fd < 0) {
         kernel_bridge_log("Kernel Bridge: Failed to create netlink socket: %s\n", strerror(errno));
         return -1;
     }
     
     /* Bind socket */
     memset(&src_addr, 0, sizeof(src_addr));
     src_addr.nl_family = AF_NETLINK;
     src_addr.nl_pid = getpid();
     src_addr.nl_groups = 0;
     
     if (bind(netlink_fd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
         kernel_bridge_log("Kernel Bridge: Failed to bind netlink socket: %s\n", strerror(errno));
         close(netlink_fd);
         netlink_fd = -1;
         return -1;
     }
     
     kernel_bridge_log("Kernel Bridge: Netlink communication initialized\n");
     return 0;
 }
 
 /* Send response via netlink */
 int kernel_bridge_send_netlink_response(int request_id, const char *interpreted_cmd, 
                                        int result_code) {
     struct ai_netlink_msg msg;
     struct sockaddr_nl dest_addr;
     struct iovec iov;
     struct msghdr msgh;
     
     if (netlink_fd < 0) {
         kernel_bridge_log("Kernel Bridge: send_netlink_response called with invalid fd\n");
         return -1;
     }
     
     /* Prepare message */
     memset(&msg, 0, sizeof(msg));
     msg.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(msg) - sizeof(struct nlmsghdr));
     msg.nlh.nlmsg_pid = getpid();
     msg.nlh.nlmsg_flags = 0;
     msg.msg_type = AI_OS_MSG_RESPONSE;
     msg.request_id = request_id;
     
     if (interpreted_cmd) {
         strncpy(msg.data, interpreted_cmd, sizeof(msg.data) - 1);
     }
     
     /* Prepare destination */
     memset(&dest_addr, 0, sizeof(dest_addr));
     dest_addr.nl_family = AF_NETLINK;
     dest_addr.nl_pid = 0; /* Kernel */
     dest_addr.nl_groups = 0;
     
     /* Prepare message header */
     iov.iov_base = &msg;
     iov.iov_len = msg.nlh.nlmsg_len;
     
     msgh.msg_name = &dest_addr;
     msgh.msg_namelen = sizeof(dest_addr);
     msgh.msg_iov = &iov;
     msgh.msg_iovlen = 1;
     msgh.msg_control = NULL;
     msgh.msg_controllen = 0;
     msgh.msg_flags = 0;
     
     /* Send message */
     if (sendmsg(netlink_fd, &msgh, 0) < 0) {
         kernel_bridge_log("Kernel Bridge: Failed to send netlink message: %s\n", strerror(errno));
         return -1;
     }
     
     return 0;
 }
 
 /* Receive request via netlink */
 int kernel_bridge_receive_netlink_request(struct ai_os_request *request) {
     struct ai_netlink_msg msg;
     struct sockaddr_nl src_addr;
     struct iovec iov;
     struct msghdr msgh;
     ssize_t len;
     
     if (netlink_fd < 0 || !request) {
         kernel_bridge_log("Kernel Bridge: receive_netlink_request called with invalid fd or null request\n");
         return -1;
     }
     
     /* Prepare to receive */
     iov.iov_base = &msg;
     iov.iov_len = sizeof(msg);
     
     msgh.msg_name = &src_addr;
     msgh.msg_namelen = sizeof(src_addr);
     msgh.msg_iov = &iov;
     msgh.msg_iovlen = 1;
     msgh.msg_control = NULL;
     msgh.msg_controllen = 0;
     msgh.msg_flags = 0;
     
     /* Receive message */
     len = recvmsg(netlink_fd, &msgh, MSG_DONTWAIT);
     if (len < 0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK) {
             return 0; /* No message available */
         }
         kernel_bridge_log("Kernel Bridge: Failed to receive netlink message: %s\n", strerror(errno));
         return -1;
     }
     
     /* Parse message */
     if (msg.msg_type == AI_OS_MSG_INTERPRET) {
         request->request_id = msg.request_id;
         request->pid = msg.pid;
         strncpy(request->command, msg.data, sizeof(request->command) - 1);
         request->command[sizeof(request->command) - 1] = '\0';
         request->timestamp = time(NULL);
         
         return 1; /* Request received */
     }
     
     return 0; /* No relevant message */
 }
 
 /* Enhanced bridge thread with netlink support */
 static void *enhanced_bridge_thread_func(void *arg) {
     struct ai_os_request request;
     struct ai_os_response response;
     fd_set readfds;
     struct timeval timeout;
     int result;
     int max_fd;
     
     kernel_bridge_log("Kernel Bridge: Enhanced bridge thread started\n");
     
     while (bridge_state.running) {
         /* Prepare file descriptor set */
         FD_ZERO(&readfds);
         max_fd = 0;
         
         if (bridge_state.kernel_fd >= 0) {
             FD_SET(bridge_state.kernel_fd, &readfds);
             max_fd = bridge_state.kernel_fd;
         }
         
         if (netlink_fd >= 0) {
             FD_SET(netlink_fd, &readfds);
             if (netlink_fd > max_fd) max_fd = netlink_fd;
         }
         
         timeout.tv_sec = 1;
         timeout.tv_usec = 0;
         
         result = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
         
         if (result < 0) {
             if (errno == EINTR) continue;
             kernel_bridge_log("Kernel Bridge: Select error: %s\n", strerror(errno));
             break;
         }
         
         if (result == 0) continue; /* Timeout */
         
         /* Check netlink socket */
         if (netlink_fd >= 0 && FD_ISSET(netlink_fd, &readfds)) {
             result = kernel_bridge_receive_netlink_request(&request);
             if (result > 0) {
                 /* Process the request */
                 if (process_kernel_request(&request, &response) == 0) {
                     kernel_bridge_send_netlink_response(request.request_id, 
                                                       response.interpreted_command, 
                                                       response.result_code);
                 }
             }
         }
         
         /* Check proc interface */
         if (bridge_state.kernel_fd >= 0 && FD_ISSET(bridge_state.kernel_fd, &readfds)) {
             /* Handle proc interface communication */
             /* This would be implementation-specific */
         }
     }
     
     kernel_bridge_log("Kernel Bridge: Enhanced bridge thread terminated\n");
     return NULL;
 }
 
 /* Start enhanced bridge with netlink support */
 int kernel_bridge_start_enhanced(void) {
     /* Initialize both proc and netlink interfaces */
     if (kernel_bridge_init() != 0) {
         kernel_bridge_log("Kernel Bridge: Failed to initialize proc interface\n");
     }
     
     if (kernel_bridge_init_netlink() != 0) {
         kernel_bridge_log("Kernel Bridge: Failed to initialize netlink interface\n");
     }
     
     if (bridge_state.kernel_fd < 0 && netlink_fd < 0) {
         kernel_bridge_log("Kernel Bridge: No communication interface available\n");
         return -1;
     }
     
     bridge_state.running = 1;
     
     if (pthread_create(&bridge_state.bridge_thread, NULL, enhanced_bridge_thread_func, NULL) != 0) {
         kernel_bridge_log("Kernel Bridge: Failed to create enhanced bridge thread: %s\n", 
                 strerror(errno));
         bridge_state.running = 0;
         return -1;
     }
     
     kernel_bridge_log("Kernel Bridge: Enhanced bridge started successfully\n");
     return 0;
 }
 
 /* Cleanup enhanced bridge */
 void kernel_bridge_cleanup_enhanced(void) {
     kernel_bridge_cleanup();
     
     if (netlink_fd >= 0) {
         if (close(netlink_fd) != 0) {
             kernel_bridge_log("Kernel Bridge: Failed to close netlink fd: %s\n", strerror(errno));
         }
         netlink_fd = -1;
     }
 }

// TODO: Add signal handling for graceful shutdown
// TODO: Add protocol validation and error escalation for repeated failures
// TODO: Use atomic operations for bridge_state.running if accessed from multiple threads