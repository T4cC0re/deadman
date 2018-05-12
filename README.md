# deadman

### Simple kernel module to kill your machine if sh\*it happened.

###### Note: This is very hacky. Also my first steps into writing kernel-space code. So please be gentile :)

### Stuff it can do:

 - ping a host periodically
 - watch for unresponsiveness/suspension
 - suicide on rmmod

### Stuff I wanna fix:

 - not use user-land calls to ping to check availability of a host.
 - optionally make sure test conditions are met at least once before engaging
 - more...

### Usage:

```
make                # You need to `make clean` if you updated you kernel before building
modinfo deadman.ko  # To see what it's like
insmod deadman.ko   # Hope it does not blow up unexpectedly :)
```

### License:
GPLv2. I would use the Apache-2.0 license, but some symbols in the kernel require GPL compliance.
