**What is this?**

This repository contains a testcase for opening pipes for reading and writting
multiples times in Cygwin.

The testcase is available in both Python and C++. Use whichever you're comfortable with.

**Instructions:**

```
git clone https://github.com/joaoe/cygwin-pipe-testcase.git
cd cygwin-pipe-testcase
./run-all
```

**Behavior**

Current behavior on Cygwin:
```
0. open(r) = /tmp/cyg_pipe_test -> 8
0. poll    = <empty>
0. read    = errno ECOMM(70): Communication error on send
1. open(w) = /tmp/cyg_pipe_test -> 9
1. poll    = 8: POLLIN(0x1)
1. read    = b'test 1'
2. poll    = 8: POLLIN(0x1)|POLLPRI(0x2)
2. read    = b''
3. open(w) = /tmp/cyg_pipe_test -> errno ENXIO(6): No such device or address
3. poll    = 8: POLLIN(0x1)|POLLPRI(0x2)
3. read    = b''
4. poll    = 8: POLLIN(0x1)|POLLPRI(0x2)
4. read    = b''
FAIL: got 5 errors
```

Expected behavior on Linux (ignore the difference in file descriptor values):
```
0. open(r) = /tmp/cyg_pipe_test -> 3
0. poll    = <empty>
0. read    = b''
1. open(w) = /tmp/cyg_pipe_test -> 4
1. poll    = 3: POLLIN(0x1)
1. read    = b'test 1'
2. poll    = 3: POLLHUP(0x10)
2. read    = b''
3. open(w) = /tmp/cyg_pipe_test -> 4
3. poll    = 3: POLLIN(0x1)
3. read    = b'test 2'
4. poll    = 3: POLLHUP(0x10)
4. read    = b''
PASS
```

**Conclusions:**

* `read()` on a non-blocking pipe opened for reading without a connected writer returns the error `ECOMM`, while it should just return a read of size 0.
* _the pipe is opened in write and non-blocking mode_
* `poll()` on the read FD sets `POLLPRI` which is behavior not specified in the `posix` specification for pipes.
* _the file descriptor for writting is closed, the read descriptor is left open_
* `poll()` will continue to report `POLLIN|POLLPRI` after the writer has disconnected, instead of reporting `POLLHUP`. `POLLHUP`, `POLLERR` and `POLLNVAL` can be returned even though they have not been requested when calling `poll()`. `POLLHUP` means that the writer has closed the pipe.
* a second `open()` of the pipe in write and non-blocking mode fails with the error `ENXIO`, instead of returning a new valid file descriptor. The same attempt but opening in blocking mode just blocks the `open()` indefinitely. The `ENXIO` error should be reported when opening a pipe in non-blocking and write mode while there is not a reader connected.
