#!/usr/bin/env python3

import errno
import os
import select
import sys

def make_pipe(pipe_path):
	try:
		os.remove(pipe_path)
	except OSError:
		pass
	try:
		os.makedirs(os.path.dirname(pipe_path), 0o600)
	except OSError:
		pass
	os.mkfifo(pipe_path, 0o600)

def get_poll_props():
	return sorted(
		(v, k)
		for (k, v) in select.__dict__.items()
			if (k.startswith("POLL") and not k.startswith("POLLRD")))

def str_event_list(event_list, poll_props):
	x = ("%d: %s" % (fd, "|".join("%s(0x%x)" % (bitname, bit) for (bit, bitname) in poll_props if (bit & bitmask)))
		for (fd, bitmask) in event_list)
	return "\n".join(x) or "<empty>"

def call_read(fd, error_count):
	try:
		return repr(os.read(fd, 1000))

	except OSError as e:
		error_count.value += 1;
		return "errno %s(%d): %s" % (errno.errorcode[e.errno], e.errno, os.strerror(e.errno))

def call_open(path, flags, error_count):
	try:
		return os.open(path, flags)

	except OSError as e:
		error_count.value += 1;
		return "errno %s(%d): %s" % (errno.errorcode[e.errno], e.errno, os.strerror(e.errno))

def main():
	class error_count:
		value = 0

	poll_props = get_poll_props()
	poll_timeout_ms = 1000

	test_pipe_path = os.path.expanduser("/tmp/cyg_pipe_test")
	make_pipe(test_pipe_path)

	nonblock_flags = os.O_NONBLOCK;
	# nonblock_flags = 0;

	if True:
		# Open read pipe
		read_fd = os.open(test_pipe_path, os.O_RDONLY | os.O_NONBLOCK)
		read_poll_obj = select.poll()
		read_poll_obj.register(read_fd, select.POLLIN | select.POLLPRI | select.POLLHUP)

		event_list = read_poll_obj.poll(poll_timeout_ms)
		if (event_list):
			error_count.value += 1

		print("0. open(r) = %s -> %d" % (test_pipe_path, read_fd))
		print("0. poll    = " + str_event_list(event_list, poll_props))
		print("0. read    = " + call_read(read_fd, error_count))

	if True:
		# Open write pipe once
		write_fd = call_open(test_pipe_path, os.O_WRONLY | nonblock_flags, error_count)
		if (isinstance(write_fd, int)):
			os.write(write_fd, b"test 1")
		else:
			error_count.value += 1

		# See what the read poll says with the writer open
		event_list = read_poll_obj.poll(poll_timeout_ms)
		if ((event_list[0][1] & select.POLLIN) == 0):
			error_count.value += 1

		print("1. open(w) = %s -> %s" % (test_pipe_path, write_fd))
		print("1. poll    = " + str_event_list(event_list, poll_props))
		print("1. read    = " + call_read(read_fd, error_count))

		# os.system("ls -la /proc/%d/fd/" % os.getpid())

		# ... and close
		if (isinstance(write_fd, int)):
			os.close(write_fd)

	if True:
		# See what the read poll says with the writer closed
		event_list = read_poll_obj.poll(poll_timeout_ms)
		if (event_list[0][1] != select.POLLHUP):
			error_count.value += 1

		print("2. poll    = " + str_event_list(event_list, poll_props))
		print("2. read    = " + call_read(read_fd, error_count))

	if True:
		# os.system("ls -la /proc/%d/fd/" % os.getpid())

		# Open write pipe a second time
		write_fd = call_open(test_pipe_path, os.O_WRONLY | nonblock_flags, error_count)
		if (isinstance(write_fd, int)):
			os.write(write_fd, b"test 2")
		else:
			error_count.value += 1

		# See what the read poll says with the writer open
		event_list = read_poll_obj.poll(poll_timeout_ms)
		if ((event_list[0][1] & select.POLLIN) == 0):
			error_count.value += 1

		print("3. open(w) = %s -> %s" % (test_pipe_path, write_fd))
		print("3. poll    = " + str_event_list(event_list, poll_props))
		print("3. read    = " + call_read(read_fd, error_count))

		# ... and close
		if (isinstance(write_fd, int)):
			os.close(write_fd)

	if True:
		# See what the read poll says with the writer closed
		event_list = read_poll_obj.poll(poll_timeout_ms)
		if (event_list[0][1] != select.POLLHUP):
			error_count.value += 1

		print("4. poll    = " + str_event_list(event_list, poll_props))
		print("4. read    = " + call_read(read_fd, error_count))

	if (error_count.value == 0):
		print("PASS")

	else:
		print("FAIL: got %d errors" % error_count.value)

if (__name__ == "__main__"):
	try:
		main()
	except (BrokenPipeError, KeyboardInterrupt):
		sys.stderr.close()
		sys.exit(1)
