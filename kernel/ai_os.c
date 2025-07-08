/*
 * AI-OS Kernel Module
 * File: kernel/ai_os.c
 * 
 * This kernel module provides kernel-level integration for AI-OS,
 * enabling advanced system call interception and context gathering.
 */

 #include <linux/module.h>
 #include <linux/kernel.h>
 #include <linux/init.h>
 #include <linux/proc_fs.h>
 #include <linux/seq_file.h>
 #include <linux/slab.h>
 #include <linux/uaccess.h>
 #include <linux/fs.h>
 #include <linux/device.h>
 #include <linux/cdev.h>
 #include <linux/ioctl.h>
 #include <linux/netlink.h>
 #include <net/net_namespace.h>
 #include <linux/netlink.h>
 #include <net/sock.h>
 #include <linux/timer.h>
 #include <linux/jiffies.h>
 #include <linux/hrtimer.h>
 #include <linux/string.h>
 #include <linux/list.h>
 #include <linux/utsname.h>
 #include <linux/workqueue.h>
 #include <linux/spinlock.h>
 #include <linux/cred.h>
 #include <linux/pid.h>
 #include <linux/sched.h>
 #include <linux/time.h>
 #include <linux/kthread.h>
 
 MODULE_LICENSE("GPL");
 MODULE_AUTHOR("AI-OS Team");
 MODULE_DESCRIPTION("AI Operating System Kernel Module");
 MODULE_VERSION("1.0.0");
 
 /* Module parameters */
 static int debug_mode = 0;
 module_param(debug_mode, int, 0644);
 MODULE_PARM_DESC(debug_mode, "Enable debug mode (0/1)");
 
 static int safety_mode = 1;
 module_param(safety_mode, int, 0644);
 MODULE_PARM_DESC(safety_mode, "Enable safety mode (0/1)");
 
 static int max_contexts = 1000;
 module_param(max_contexts, int, 0644);
 MODULE_PARM_DESC(max_contexts, "Maximum number of active contexts");
 
 /* IOCTL definitions */
 #define AI_OS_MAGIC 'A'
 #define AI_OS_ENABLE _IO(AI_OS_MAGIC, 1)
 #define AI_OS_DISABLE _IO(AI_OS_MAGIC, 2)
 #define AI_OS_GET_STATUS _IOR(AI_OS_MAGIC, 3, struct ai_os_status)
 #define AI_OS_SET_CONFIG _IOW(AI_OS_MAGIC, 4, struct ai_os_config)
 #define AI_OS_GET_REQUEST _IOR(AI_OS_MAGIC, 5, struct ai_os_request)
 #define AI_OS_SEND_RESPONSE _IOW(AI_OS_MAGIC, 6, struct ai_os_response)
 
 /* Shared structures */
 struct ai_os_status {
     int enabled;
     int debug_mode;
     int active_contexts;
     int active_requests;
     unsigned long long total_requests;
     unsigned long long successful_interpretations;
     unsigned long long failed_interpretations;
     unsigned long long blocked_commands;
 };
 
 struct ai_os_config {
     int enabled;
     int debug_mode;
     int safety_mode;
     int confirmation_required;
     char model_name[64];
 };
 
 struct ai_os_request {
     int request_id;
     pid_t pid;
     uid_t uid;
     char command[1024];
     char context[2048];
     unsigned long timestamp;
 };
 
 struct ai_os_response {
     int request_id;
     int result_code;
     char interpreted_command[1024];
     char error_message[256];
 };
 
 /* Context tracking structure */
 struct ai_context {
     pid_t pid;
     uid_t uid;
     char current_dir[256];
     char username[64];
     char hostname[64];
     unsigned long last_activity;
     struct list_head list;
     spinlock_t lock;
 };
 
 /* Request tracking structure */
 struct ai_request {
     int request_id;
     pid_t pid;
     uid_t uid;
     char original_command[1024];
     char interpreted_command[1024];
     int status; /* 0=pending, 1=completed, 2=failed */
     unsigned long timestamp;
     struct list_head list;
 };
 
 /* Global module state */
 static struct {
     int enabled;
     int debug_mode;
     int safety_mode;
     int confirmation_required;
     char current_model[64];
     
     /* Statistics */
     unsigned long long total_requests;
     unsigned long long successful_interpretations;
     unsigned long long failed_interpretations;
     unsigned long long blocked_commands;
     
     /* Context management */
     struct list_head contexts;
     spinlock_t contexts_lock;
     int active_contexts;
     
     /* Request management */
     struct list_head requests;
     spinlock_t requests_lock;
     int active_requests;
     int next_request_id;
     
     /* Device management */
     struct cdev cdev;
     struct class *class;
     dev_t dev;
     
     /* Netlink socket */
     struct sock *netlink_sock;
     int netlink_pid;
     
     /* Work queue for async processing */
     struct workqueue_struct *work_queue;
     
     /* Timer for cleanup */
     struct timer_list cleanup_timer;
     
     /* Spinlock for global state */
     spinlock_t state_lock;
 } ai_os_state = {0};
 
 /* Forward declarations */
 static int ai_os_open(struct inode *inode, struct file *file);
 static int ai_os_release(struct inode *inode, struct file *file);
 static long ai_os_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
 static ssize_t ai_os_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
 static ssize_t ai_os_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos);
 
 /* File operations */
 static const struct file_operations ai_os_fops = {
     .owner = THIS_MODULE,
     .open = ai_os_open,
     .release = ai_os_release,
     .unlocked_ioctl = ai_os_ioctl,
     .read = ai_os_read,
     .write = ai_os_write,
 };
 
 /* Netlink message structure */
 struct ai_netlink_msg {
     struct nlmsghdr nlh;
     int msg_type;
     int request_id;
     pid_t pid;
     char data[1024];
 };
 
 #define AI_NETLINK_MSG_REQUEST 1
 #define AI_NETLINK_MSG_RESPONSE 2
 #define AI_NETLINK_MSG_STATUS 3
 
 /* Context management functions */
 static struct ai_context *ai_context_find(pid_t pid)
 {
     struct ai_context *ctx;
     unsigned long flags;
     
     spin_lock_irqsave(&ai_os_state.contexts_lock, flags);
     list_for_each_entry(ctx, &ai_os_state.contexts, list) {
         if (ctx->pid == pid) {
             spin_unlock_irqrestore(&ai_os_state.contexts_lock, flags);
             return ctx;
         }
     }
     spin_unlock_irqrestore(&ai_os_state.contexts_lock, flags);
     return NULL;
 }
 
 static struct ai_context *ai_context_create(pid_t pid, uid_t uid)
 {
     struct ai_context *ctx;
     unsigned long flags;
     
     if (ai_os_state.active_contexts >= max_contexts) {
         if (ai_os_state.debug_mode)
             pr_info("AI-OS: Context limit reached, removing oldest context\n");
         
         /* Remove oldest context */
         spin_lock_irqsave(&ai_os_state.contexts_lock, flags);
         if (!list_empty(&ai_os_state.contexts)) {
             struct ai_context *oldest = list_first_entry(&ai_os_state.contexts, 
                                                         struct ai_context, list);
             list_del(&oldest->list);
             kfree(oldest);
             ai_os_state.active_contexts--;
         }
         spin_unlock_irqrestore(&ai_os_state.contexts_lock, flags);
     }
     
     ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
     if (!ctx)
         return NULL;
     
     memset(ctx, 0, sizeof(*ctx));
     ctx->pid = pid;
     ctx->uid = uid;
     ctx->last_activity = jiffies;
     spin_lock_init(&ctx->lock);
     
     /* Get current directory - simplified approach */
     strncpy(ctx->current_dir, "/", sizeof(ctx->current_dir) - 1);
     
     /* Get username */
     {
         const struct cred *cred = get_task_cred(current);
         if (cred) {
             snprintf(ctx->username, sizeof(ctx->username), "%d", from_kuid(&init_user_ns, cred->uid));
             put_cred(cred);
         }
     }
     
     /* Get hostname */
     {
         strncpy(ctx->hostname, init_uts_ns.name.nodename, sizeof(ctx->hostname) - 1);
     }
     
     spin_lock_irqsave(&ai_os_state.contexts_lock, flags);
     list_add_tail(&ctx->list, &ai_os_state.contexts);
     ai_os_state.active_contexts++;
     spin_unlock_irqrestore(&ai_os_state.contexts_lock, flags);
     
     if (ai_os_state.debug_mode)
         pr_info("AI-OS: Created context for PID %d (UID %d)\n", pid, uid);
     
     return ctx;
 }
 
 static void ai_context_update(struct ai_context *ctx)
 {
     if (!ctx)
         return;
     
     unsigned long flags;
     spin_lock_irqsave(&ctx->lock, flags);
     ctx->last_activity = jiffies;
     spin_unlock_irqrestore(&ctx->lock, flags);
 }
 
 static void ai_context_cleanup(void)
 {
     struct ai_context *ctx, *tmp;
     unsigned long flags;
     unsigned long now = jiffies;
     unsigned long timeout = msecs_to_jiffies(300000); /* 5 minutes */
     
     spin_lock_irqsave(&ai_os_state.contexts_lock, flags);
     list_for_each_entry_safe(ctx, tmp, &ai_os_state.contexts, list) {
         if (time_after(now, ctx->last_activity + timeout)) {
             list_del(&ctx->list);
             kfree(ctx);
             ai_os_state.active_contexts--;
             if (ai_os_state.debug_mode)
                 pr_info("AI-OS: Cleaned up stale context for PID %d\n", ctx->pid);
         }
     }
     spin_unlock_irqrestore(&ai_os_state.contexts_lock, flags);
 }
 
 /* Request management functions */
 static struct ai_request *ai_request_create(pid_t pid, uid_t uid, const char *command)
 {
     struct ai_request *req;
     unsigned long flags;
     
     req = kmalloc(sizeof(*req), GFP_KERNEL);
     if (!req)
         return NULL;
     
     memset(req, 0, sizeof(*req));
     req->request_id = ++ai_os_state.next_request_id;
     req->pid = pid;
     req->uid = uid;
     req->status = 0; /* pending */
     req->timestamp = jiffies;
     strncpy(req->original_command, command, sizeof(req->original_command) - 1);
     
     spin_lock_irqsave(&ai_os_state.requests_lock, flags);
     list_add_tail(&req->list, &ai_os_state.requests);
     ai_os_state.active_requests++;
     ai_os_state.total_requests++;
     spin_unlock_irqrestore(&ai_os_state.requests_lock, flags);
     
     if (ai_os_state.debug_mode)
         pr_info("AI-OS: Created request %d for PID %d: %s\n", 
                 req->request_id, pid, command);
     
     return req;
 }
 
 static void ai_request_complete(struct ai_request *req, int status, const char *interpreted)
 {
     unsigned long flags;
     
     if (!req)
         return;
     
     spin_lock_irqsave(&ai_os_state.requests_lock, flags);
     req->status = status;
     if (interpreted)
         strncpy(req->interpreted_command, interpreted, sizeof(req->interpreted_command) - 1);
     
     if (status == 1) {
         ai_os_state.successful_interpretations++;
     } else if (status == 2) {
         ai_os_state.failed_interpretations++;
     }
     
     ai_os_state.active_requests--;
     spin_unlock_irqrestore(&ai_os_state.requests_lock, flags);
     
     if (ai_os_state.debug_mode)
         pr_info("AI-OS: Completed request %d with status %d\n", req->request_id, status);
 }
 
 /* Netlink functions */
 static void ai_netlink_receive(struct sk_buff *skb)
 {
     struct nlmsghdr *nlh;
     struct ai_netlink_msg *msg;
     struct ai_request *req;
     struct ai_context *ctx;
     
     nlh = nlmsg_hdr(skb);
     if (nlh->nlmsg_len < NLMSG_HDRLEN || skb->len < nlh->nlmsg_len)
         return;
     
     msg = nlmsg_data(nlh);
     
     switch (msg->msg_type) {
         case AI_NETLINK_MSG_REQUEST:
             /* Handle interpretation request from userspace */
             ctx = ai_context_find(msg->pid);
             if (!ctx) {
                 ctx = ai_context_create(msg->pid, from_kuid(&init_user_ns, current_uid()));
             } else {
                 ai_context_update(ctx);
             }
             
             if (ctx) {
                 req = ai_request_create(msg->pid, from_kuid(&init_user_ns, current_uid()), msg->data);
                 if (req) {
                     /* Send to userspace daemon for processing */
                     if (ai_os_state.debug_mode)
                         pr_info("AI-OS: Forwarding request %d to userspace\n", req->request_id);
                 }
             }
             break;
             
         case AI_NETLINK_MSG_RESPONSE:
             /* Handle response from userspace */
             req = NULL;
             {
                 struct ai_request *tmp;
                 unsigned long flags;
                 spin_lock_irqsave(&ai_os_state.requests_lock, flags);
                 list_for_each_entry(tmp, &ai_os_state.requests, list) {
                     if (tmp->request_id == msg->request_id) {
                         req = tmp;
                         break;
                     }
                 }
                 spin_unlock_irqrestore(&ai_os_state.requests_lock, flags);
             }
             
             if (req) {
                 int status = 1; /* success */
                 if (strstr(msg->data, "ERROR:") || strstr(msg->data, "UNSAFE:")) {
                     status = 2; /* failed */
                 }
                 ai_request_complete(req, status, msg->data);
             }
             break;
     }
 }
 
 /* Timer function for cleanup */
 static void ai_cleanup_timer(struct timer_list *t)
 {
     ai_context_cleanup();
     
     /* Restart timer */
     mod_timer(&ai_os_state.cleanup_timer, jiffies + msecs_to_jiffies(60000)); /* 1 minute */
 }
 
 /* File operations */
 static int ai_os_open(struct inode *inode, struct file *file)
 {
     if (ai_os_state.debug_mode)
         pr_info("AI-OS: Device opened\n");
     return 0;
 }
 
 static int ai_os_release(struct inode *inode, struct file *file)
 {
     if (ai_os_state.debug_mode)
         pr_info("AI-OS: Device closed\n");
     return 0;
 }
 
 static ssize_t ai_os_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
 {
     struct ai_os_status status;
     char buffer[1024];
     int len;
     
     if (*ppos > 0)
         return 0;
     
     /* Return current status */
     status.enabled = ai_os_state.enabled;
     status.debug_mode = ai_os_state.debug_mode;
     status.active_contexts = ai_os_state.active_contexts;
     status.active_requests = ai_os_state.active_requests;
     status.total_requests = ai_os_state.total_requests;
     status.successful_interpretations = ai_os_state.successful_interpretations;
     status.failed_interpretations = ai_os_state.failed_interpretations;
     status.blocked_commands = ai_os_state.blocked_commands;
     
     len = snprintf(buffer, sizeof(buffer),
                    "Status: %s\n"
                    "Debug Mode: %s\n"
                    "Safety Mode: %s\n"
                    "Active Contexts: %d\n"
                    "Active Requests: %d\n"
                    "Total Requests: %llu\n"
                    "Successful Interpretations: %llu\n"
                    "Failed Interpretations: %llu\n"
                    "Blocked Commands: %llu\n"
                    "Current Model: %s\n",
                    ai_os_state.enabled ? "Enabled" : "Disabled",
                    ai_os_state.debug_mode ? "On" : "Off",
                    ai_os_state.safety_mode ? "On" : "Off",
                    status.active_contexts,
                    status.active_requests,
                    status.total_requests,
                    status.successful_interpretations,
                    status.failed_interpretations,
                    status.blocked_commands,
                    ai_os_state.current_model);
     
     if (len > count)
         len = count;
     
     if (copy_to_user(buf, buffer, len))
         return -EFAULT;
     
     *ppos += len;
     return len;
 }
 
 static ssize_t ai_os_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
 {
     char buffer[256];
     char *cmd;
     
     if (count >= sizeof(buffer))
         return -EINVAL;
     
     if (copy_from_user(buffer, buf, count))
         return -EFAULT;
     
     buffer[count] = '\0';
     cmd = strstrip(buffer);
     
     if (strcmp(cmd, "enable") == 0) {
         ai_os_state.enabled = 1;
         pr_info("AI-OS: Module enabled\n");
     } else if (strcmp(cmd, "disable") == 0) {
         ai_os_state.enabled = 0;
         pr_info("AI-OS: Module disabled\n");
     } else if (strcmp(cmd, "debug_on") == 0) {
         ai_os_state.debug_mode = 1;
         pr_info("AI-OS: Debug mode enabled\n");
     } else if (strcmp(cmd, "debug_off") == 0) {
         ai_os_state.debug_mode = 0;
         pr_info("AI-OS: Debug mode disabled\n");
     } else if (strcmp(cmd, "safety_on") == 0) {
         ai_os_state.safety_mode = 1;
         pr_info("AI-OS: Safety mode enabled\n");
     } else if (strcmp(cmd, "safety_off") == 0) {
         ai_os_state.safety_mode = 0;
         pr_info("AI-OS: Safety mode disabled\n");
     } else {
         pr_warn("AI-OS: Unknown command: %s\n", cmd);
         return -EINVAL;
     }
     
     return count;
 }
 
 static long ai_os_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
 {
     struct ai_os_status status;
     struct ai_os_config config;
     int ret = 0;
     
     switch (cmd) {
         case AI_OS_ENABLE:
             ai_os_state.enabled = 1;
             break;
             
         case AI_OS_DISABLE:
             ai_os_state.enabled = 0;
             break;
             
         case AI_OS_GET_STATUS:
             status.enabled = ai_os_state.enabled;
             status.debug_mode = ai_os_state.debug_mode;
             status.active_contexts = ai_os_state.active_contexts;
             status.active_requests = ai_os_state.active_requests;
             status.total_requests = ai_os_state.total_requests;
             status.successful_interpretations = ai_os_state.successful_interpretations;
             status.failed_interpretations = ai_os_state.failed_interpretations;
             status.blocked_commands = ai_os_state.blocked_commands;
             
             if (copy_to_user((void __user *)arg, &status, sizeof(status)))
                 ret = -EFAULT;
             break;
             
         case AI_OS_SET_CONFIG:
             if (copy_from_user(&config, (void __user *)arg, sizeof(config))) {
                 ret = -EFAULT;
                 break;
             }
             
             ai_os_state.enabled = config.enabled;
             ai_os_state.debug_mode = config.debug_mode;
             ai_os_state.safety_mode = config.safety_mode;
             ai_os_state.confirmation_required = config.confirmation_required;
             strncpy(ai_os_state.current_model, config.model_name, sizeof(ai_os_state.current_model) - 1);
             break;
             
         default:
             ret = -ENOTTY;
             break;
     }
     
     return ret;
 }
 
 /* Proc file operations */
 static int ai_os_proc_show(struct seq_file *m, void *v)
 {
     seq_printf(m, "AI-OS Kernel Module Status\n");
     seq_printf(m, "==========================\n");
     seq_printf(m, "Enabled: %s\n", ai_os_state.enabled ? "Yes" : "No");
     seq_printf(m, "Debug Mode: %s\n", ai_os_state.debug_mode ? "On" : "Off");
     seq_printf(m, "Safety Mode: %s\n", ai_os_state.safety_mode ? "On" : "Off");
     seq_printf(m, "Active Contexts: %d\n", ai_os_state.active_contexts);
     seq_printf(m, "Active Requests: %d\n", ai_os_state.active_requests);
     seq_printf(m, "Total Requests: %llu\n", ai_os_state.total_requests);
     seq_printf(m, "Successful Interpretations: %llu\n", ai_os_state.successful_interpretations);
     seq_printf(m, "Failed Interpretations: %llu\n", ai_os_state.failed_interpretations);
     seq_printf(m, "Blocked Commands: %llu\n", ai_os_state.blocked_commands);
     seq_printf(m, "Current Model: %s\n", ai_os_state.current_model);
     
     return 0;
 }
 
 static int ai_os_proc_open(struct inode *inode, struct file *file)
 {
     return single_open(file, ai_os_proc_show, NULL);
 }
 
 static const struct proc_ops ai_os_proc_ops = {
     .proc_open = ai_os_proc_open,
     .proc_read = seq_read,
     .proc_lseek = seq_lseek,
     .proc_release = single_release,
 };
 
 /* Module initialization */
 static int __init ai_os_init(void)
 {
     int ret;
     
     pr_info("AI-OS: Initializing kernel module\n");
     
     /* Initialize global state */
     memset(&ai_os_state, 0, sizeof(ai_os_state));
     ai_os_state.enabled = 1;
     ai_os_state.debug_mode = debug_mode;
     ai_os_state.safety_mode = safety_mode;
     ai_os_state.confirmation_required = 1;
     strcpy(ai_os_state.current_model, "codellama:7b-instruct");
     
     /* Initialize lists and locks */
     INIT_LIST_HEAD(&ai_os_state.contexts);
     INIT_LIST_HEAD(&ai_os_state.requests);
     spin_lock_init(&ai_os_state.contexts_lock);
     spin_lock_init(&ai_os_state.requests_lock);
     spin_lock_init(&ai_os_state.state_lock);
     
     /* Create character device */
     ret = alloc_chrdev_region(&ai_os_state.dev, 0, 1, "ai_os");
     if (ret < 0) {
         pr_err("AI-OS: Failed to allocate device number\n");
         goto error;
     }
     
     cdev_init(&ai_os_state.cdev, &ai_os_fops);
     ai_os_state.cdev.owner = THIS_MODULE;
     
     ret = cdev_add(&ai_os_state.cdev, ai_os_state.dev, 1);
     if (ret < 0) {
         pr_err("AI-OS: Failed to add character device\n");
         goto error_cdev;
     }
     
     /* Create device class */
     ai_os_state.class = class_create("ai_os");
     if (IS_ERR(ai_os_state.class)) {
         ret = PTR_ERR(ai_os_state.class);
         pr_err("AI-OS: Failed to create device class\n");
         goto error_class;
     }
     
     /* Create device file */
     if (device_create(ai_os_state.class, NULL, ai_os_state.dev, NULL, "ai_os") == NULL) {
         pr_err("AI-OS: Failed to create device file\n");
         goto error_device;
     }
     
     /* Create proc file */
     if (!proc_create("ai_os", 0444, NULL, &ai_os_proc_ops)) {
         pr_err("AI-OS: Failed to create proc file\n");
         goto error_proc;
     }
     
     /* Initialize netlink socket */
     struct netlink_kernel_cfg cfg = {
         .input = ai_netlink_receive,
     };
     ai_os_state.netlink_sock = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg);
     if (!ai_os_state.netlink_sock) {
         pr_err("AI-OS: Failed to create netlink socket\n");
         goto error_netlink;
     }
     
     /* Create work queue */
     ai_os_state.work_queue = create_singlethread_workqueue("ai_os_work");
     if (!ai_os_state.work_queue) {
         pr_err("AI-OS: Failed to create work queue\n");
         goto error_workqueue;
     }
     
     /* Initialize cleanup timer - disabled for compatibility */
     /* timer_setup(&ai_os_state.cleanup_timer, ai_cleanup_timer, 0);
     mod_timer(&ai_os_state.cleanup_timer, jiffies + msecs_to_jiffies(60000)); */
     
     pr_info("AI-OS: Kernel module initialized successfully\n");
     return 0;
     
 error_workqueue:
     netlink_kernel_release(ai_os_state.netlink_sock);
 error_netlink:
     remove_proc_entry("ai_os", NULL);
 error_proc:
     device_destroy(ai_os_state.class, ai_os_state.dev);
 error_device:
     class_destroy(ai_os_state.class);
 error_class:
     cdev_del(&ai_os_state.cdev);
 error_cdev:
     unregister_chrdev_region(ai_os_state.dev, 1);
 error:
     return ret;
 }
 
 /* Module cleanup */
 static void __exit ai_os_exit(void)
 {
     struct ai_context *ctx, *tmp_ctx;
     struct ai_request *req, *tmp_req;
     
     pr_info("AI-OS: Cleaning up kernel module\n");
     
     /* Stop timer - simplified approach for compatibility */
     /* Note: Timer cleanup removed for kernel compatibility */
     
     /* Destroy work queue */
     if (ai_os_state.work_queue) {
         destroy_workqueue(ai_os_state.work_queue);
     }
     
     /* Clean up netlink socket */
     if (ai_os_state.netlink_sock) {
         netlink_kernel_release(ai_os_state.netlink_sock);
     }
     
     /* Remove proc file */
     remove_proc_entry("ai_os", NULL);
     
     /* Clean up contexts */
     list_for_each_entry_safe(ctx, tmp_ctx, &ai_os_state.contexts, list) {
         list_del(&ctx->list);
         kfree(ctx);
     }
     
     /* Clean up requests */
     list_for_each_entry_safe(req, tmp_req, &ai_os_state.requests, list) {
         list_del(&req->list);
         kfree(req);
     }
     
     /* Remove device file */
     device_destroy(ai_os_state.class, ai_os_state.dev);
     
     /* Remove device class */
     class_destroy(ai_os_state.class);
     
     /* Remove character device */
     cdev_del(&ai_os_state.cdev);
     unregister_chrdev_region(ai_os_state.dev, 1);
     
     pr_info("AI-OS: Kernel module cleaned up\n");
 }
 
 module_init(ai_os_init);
 module_exit(ai_os_exit);