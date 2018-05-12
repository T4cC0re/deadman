#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/ioctl.h>
#include <linux/kd.h>
#include <linux/fs.h>

MODULE_LICENSE("GPL"); // Not a fan, but required to get some symbols :(
MODULE_AUTHOR("Hendrik 'T4cC0re' Meyer");
MODULE_DESCRIPTION("deadman switch");
MODULE_VERSION("0.1");

static char *host = "";
static short maxfail = 24;
static short maxUnresponsiveSecs = 30;
static short preventunload = 1;

module_param(host, charp, S_IRUGO); // charp = char ptr, S_IRUGO = RO
module_param(maxfail, short, S_IRUGO);
module_param(maxUnresponsiveSecs, short, S_IRUGO);
module_param(preventunload, short, S_IRUGO);

MODULE_PARM_DESC(host, "host to ping periodically (empty to disable, default)");
MODULE_PARM_DESC(maxfail, "Maximum amount of failure conditions (default 24 = ~120 sec.)");
MODULE_PARM_DESC(maxUnresponsiveSecs, "Maximum time the system can spend without interacting with deadman (e.g. sleep) (default: 30 sec.)");
MODULE_PARM_DESC(preventunload, "Set to 1 or 0 to enable or disable auto-kill on unload. (default: 1)");

static char *envp[] = {
  "HOME=/",
  "TERM=linux",
  "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
  NULL
};

/**
 * Wakes every 100ms to see if the kernelthread should stop. Returns true in that case.
 */
bool threadsleep(int msec) {
  int counter = (msec/100);
  while (counter > 0) {
    msleep(100);
    if (kthread_should_stop()) {
      return true;
    }
    counter --;
  }
  return false;
}

// CLOCK_TICK_RATE as defined in beep.c of the beep command
#ifndef CLOCK_TICK_RATE
#define CLOCK_TICK_RATE 1193180
#endif

/**
 * Beeps.. duh! Roughly based on beep.c, but rewritten to use kernel-space ioctls.
 */
bool beep(int freq, int duration, int delay, uint repetetions) {
  int i;
  struct file* console_fd = filp_open("/dev/console", O_WRONLY, 0);
  if (console_fd == -1) {
    printk (KERN_INFO "deadman: Can not open /dev/console\n");
    return threadsleep(repetetions*(duration+delay)); // At least keep the pacing
  }

  for (i = 0; i < repetetions; i++) {
    console_fd->f_op->unlocked_ioctl(console_fd, KIOCSOUND, (int)(CLOCK_TICK_RATE/freq));
    threadsleep(duration);
    console_fd->f_op->unlocked_ioctl(console_fd,KIOCSOUND, 0);
    if(threadsleep(delay)) break;
  }

  filp_close(console_fd,0);
  return false;
}

unsigned long long now = 0;
unsigned long long lastHeartbeat = 0;;
unsigned long long heartbeatTolerance = 0;

/**
 * Resets the heartbeat evaluated by wasUnresponsive(). Use with caution!
 */
void resetHeatbeat(void) {
  struct timespec nstimeofday;

  getnstimeofday(&nstimeofday);
  lastHeartbeat = now = timespec_to_ns(&nstimeofday);

  printk(KERN_INFO "deadman: Heartbeat reset to: %llu\n", lastHeartbeat);
}

/**
 * Returns true if two calls to this functio are more that maxUnresponsiveSecs seconds apart
 */
bool wasUnresponsive(void) {
  struct timespec nstimeofday;

  getnstimeofday(&nstimeofday);
  now = timespec_to_ns(&nstimeofday);

  if ((now - lastHeartbeat) > (heartbeatTolerance * 1000000000)) {
    printk(KERN_INFO "deadman: System hung %llu ns!\n", now - lastHeartbeat);
    return true;
  }

  lastHeartbeat = now;
  return false;
}

bool testCondition(void) {
  int ret = 0;
  char *argv[] = { "/usr/bin/ping", "-c", "1", host , NULL};

  printk(KERN_INFO "deadman: testing contidions...\n");

  if (host != "") {
    printk(KERN_INFO "deadman: pinging %s...!\n", host);
    ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC) % 255;
    printk(KERN_INFO "deadman: ping returned %d!\n", ret);
  }

  if (ret != 0) return false;

  /// TODO: More contidions

  printk(KERN_INFO "deadman: contidions passed!\n");
  return true;
}

void warn(void) {
  char *argv[] = { "/usr/bin/notify-send", "-u", "critical", "Deadman switch engaged!" , NULL};

  printk(KERN_INFO "deadman: warning the user\n");
  call_usermodehelper(argv[0], argv, envp, UMH_NO_WAIT);
}

void deadman(void) {
  printk(KERN_INFO "deadman: BAM! Deadman!\n");
  // emergency_sync();
  // emergency_remount();
  kernel_restart(NULL);
}

void deadIfUnresponsive(void) {
    if (wasUnresponsive()) deadman();
}

struct task_struct *task;
int deadcounter;

int thread(void *unused) {
  int c = 0;

  while(!kthread_should_stop()){
    if (threadsleep(5000)) return 0;
    deadIfUnresponsive();
    if (!testCondition()) {
      deadcounter--;
      printk(KERN_INFO "deadman: Failure condition! Count to deadman: %d\n", deadcounter);
      if (deadcounter <= 0) {
        warn();
        printk(KERN_INFO "deadman: Leaving the user 60 seconds to fix it...\n");

        // The following loops are to keep wasUnresponsive() in check
        for (c = 0; c < 3; c++) {
          if(beep(2500, 500, 500, 5)) return 0;
          deadIfUnresponsive();
        }

        for (c = 0; c < 9; c++){
          if(threadsleep(5000)) return 0;
          deadIfUnresponsive();
        }

        if (testCondition()){
          deadcounter = maxfail;
          printk(KERN_INFO "deadman: Failure condition repaired! Count to deadman: %d\n", deadcounter);
          continue;
        }
        deadman();
      }
    } else {
      if (deadcounter < maxfail) {
        printk(KERN_INFO "deadman: Failure condition hiccup rectified! Count to deadman: %d, %d before.\n", maxfail, deadcounter);
      } else {
        printk(KERN_INFO "deadman: No failure contidions! Count to deadman: %d\n",deadcounter);
      }
      deadcounter = maxfail;
    }
  }
  return 0;
}

static int __init init(void){
  printk(KERN_INFO "deadman: Init\n");

  if (maxUnresponsiveSecs <= 5) {
    printk(KERN_INFO, "deadman: maxUnresponsiveSecs has to be greater than 5! Setting it to 6.");
    maxUnresponsiveSecs = 6;
  }
  heartbeatTolerance = maxUnresponsiveSecs;
  resetHeatbeat();

  printk(KERN_INFO "deadman: host to ping: %s\n", host);
  printk(KERN_INFO "deadman: Maximum failures in a row: %d\n", maxfail);
  printk(KERN_INFO "deadman: Unresponsiveness: %llu\n", heartbeatTolerance);
  printk(KERN_INFO "deadman: preventunload: %d\n", preventunload);

  deadcounter = maxfail;

  task = kthread_run(&thread, 0, "deadmand");

  return 0;
}

static void __exit cleanup(void){
printk(KERN_INFO "deadman: Asked to unload. Cleaning up...\n");
  if (task != NULL) {
    kthread_stop(task);
  }
  if (preventunload == 1) {
    printk(KERN_INFO "deadman: preventunload enabled. Suicide!\n");
    deadman();
  }
  printk(KERN_INFO "deadman: Exit!\n");
}

module_init(init);
module_exit(cleanup);
