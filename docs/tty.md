## DESCLAMAIR
This one was tough, some of the paras are direct copy pastes I'd recommend reading form the link in reference.
I don't understand all of it and thus, it's likely that something I wrote here might be wrong.

## What is TTY?
It has a long history but for my purpose I can say that its "A system that manages communication between the kernel and user input/output devices". These devices would terminal emulators in my case. (I hope at least that its that :shrug:)

It handles input processing (line discipline and other such things).

## Jobs and Sessions
The basic idea is that every pipeline is a job, because every process in a pipeline should be manipulated (stopped, resumed, killed) simultaneously. That's why kill(2) allows you to send signals to entire process groups. By default, fork(2) places a newly created child process in the same process group as its parent, so that e.g. a ^C from the keyboard will affect both parent and child. But the shell, as part of its session leader duties, creates a new process group every time it launches a pipeline.

The TTY driver keeps track of the foreground process group id, but only in a passive way. The session leader has to update this information explicitly when necessary. Similarly, the TTY driver keeps track of the size of the connected terminal, but this information has to be updated explicitly, by the terminal emulator or even by the user.

The TTY will give input to only the foreground job, and only the foreground job will be able to wrtie to TTY.

## Signals and Userland communication
UNIX files including the TTY file, can be read from and written to, and manipulated by `ioctl` call.

All is fine and dandy until the kernel tries to communicate *asynchrously* with the process, and starts sending some deadly signlas(heh).

## Configuration
This was fun, I'd encourage you to try it out as well. (:laughing_face:)
- `stty -a`: Just lists out all settings for the current TTY device.
- `stty rows X columns Y`: Sets rows to *X* and columns to *Y*.
- `stty intr o`: Changes the interrupt signal to character `o`, and not `^C`.
- `stty -echo`: Usually the terminal emulator will transmit information to the kernel which the kernel will then echo back to the emulator allowing you to see what you type. `-echo` will disable that. You'll no longer see what you type but it will indeed be there.
- `stty tostop`: Controls whether background processes are allowed to write to the terminal.
 - Consider the following command `stty tostop; (sleep 5; echo hello, world) &`. It should normally print "hello, world" to the terminal after 5 seconds. But since we set *tostop*, the TTY will kill the background job at the end of 5 seconds since it's not allowed to write. 
 - If you run this command `stty -tostop; (sleep 5; echo hello, world) &`. Since now we unset the *tostop* the background process will be allowed to write to the terminal after 5 seconds and we'll see "hello, world" pop into the terminal.

## References
[TTYL Demystified](https://www.linusakesson.net/programming/tty/)
