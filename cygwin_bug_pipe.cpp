
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <vector>

typedef std::vector<std::pair<int, std::string>> VecIntString;
const VecIntString& get_poll_props() {
	static VecIntString the_vec;
	if (the_vec.size() == 0) {
#define _push_value(value) the_vec.push_back(std::pair<int, std::string>(value, #value))
		_push_value(POLLIN);
		_push_value(POLLPRI);
		_push_value(POLLHUP);
		_push_value(POLLERR);
		_push_value(POLLNVAL);
#undef _push_value
	}
	return the_vec;
}

typedef std::unordered_map<int, std::string> MapIntString;
const MapIntString& get_errno_props() {
	static MapIntString the_map;
	if (the_map.size() == 0) {
#define _push_value(value) the_map[value] = #value
		_push_value(ECOMM);
		_push_value(EAGAIN);
		_push_value(ENXIO);
		_push_value(EEXIST);
#undef _push_value
	}
	return the_map;
}

template <int N>
bool check_sys_call_n(int ret_value, int errno_accepted[N], std::string* output_buffer) {
	if (output_buffer) {
		output_buffer->erase();
	}

	if (ret_value >= 0) {
		return false;
	}

	for (int k = 0; k < N; k++) {
		if (errno_accepted[k] == errno) {
			return false;
		}
	}

	static const size_t BS = 1000;
	static char buffer[BS];

	snprintf(buffer, BS, "errno %s(%d): %s",
		get_errno_props().at(errno).c_str(), errno, strerror(errno));
	
	if (output_buffer) {
		output_buffer->assign(buffer);
		return true;
	}

	throw std::string(buffer);
}

bool check_sys_call_v(int ret_value, int errno_accepted, std::string* output_buffer) {
	int as_array[1] = {errno_accepted};
	return check_sys_call_n<1>(ret_value, as_array, output_buffer);
}

bool check_sys_call(int ret_value, std::string* output_buffer) {
	int as_array[0] = {};
	return check_sys_call_n<0>(ret_value, as_array, output_buffer);
}

void make_pipe(const std::string& pipe_path) {
	unlink(pipe_path.c_str());

	std::size_t slash = pipe_path.rfind("/");

	mode_t mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP;

	if (slash != std::string::npos) {
		std::string folder_path = pipe_path.substr(0, slash);
		check_sys_call_v(mkdir(folder_path.c_str(), mode), EEXIST, NULL);
	}

	check_sys_call(mkfifo(pipe_path.c_str(), mode), NULL);
}

template<size_t N>
const char* str_event_list(const struct pollfd (&poll_arr)[N],
	const VecIntString& poll_props, std::string *output_buffer) {

	std::stringstream ss;
	for (size_t k = 0; k < N; k++) {
		for (size_t j = 0, first = 1; j < poll_props.size(); ++j) {
			if (poll_arr[k].revents & poll_props[j].first) {
				if (first) {
					ss << (poll_arr[k].fd) << ": ";
					first = 0;
				}
				else {
					ss << "|";
				}
				ss << poll_props[j].second << "(0x" << std::hex << poll_props[j].first << ")";
			}
		}
	}
	
	output_buffer->assign(ss.str());
	if (output_buffer->size() == 0) {
		output_buffer->assign("<empty>");
	}

	return output_buffer->c_str();
}

const char* call_read(int fd, std::string *output, int *error_counter) {
	static const size_t N = 1000;
	static char buffer[N] = "b'";

	ssize_t count = read(fd, buffer + 2, N - 4);
	if (check_sys_call(count, output)) {
		(*error_counter)++;
		return output->c_str();
	}

	buffer[count + 2] = '\'';
	buffer[count + 3] = 0;

	output->assign(buffer);
	return output->c_str();
}

int call_open(const std::string& path, int flags, std::string* output, int *error_counter) {
	int fd = open(path.c_str(), flags);
	if (check_sys_call(fd, output)) {
		(*error_counter)++;
	}
	return fd;
}

void sub_main() {
	std::string output_buffer;
	const VecIntString& poll_props = get_poll_props();
	
	// This counts known rrors in cygwin vs linux.
	int error_count = 0;

	int bitmask = 0;
	for (auto iter = poll_props.begin(); iter != poll_props.end(); ++iter) {
		bitmask |= iter->first;
	}

	int poll_timeout_ms = 1000;

	std::string test_pipe_path = "/tmp/cyg_pipe_test";
	make_pipe(test_pipe_path);

	int nonblock_flags = O_NONBLOCK;
	// nonblock_flags = 0;

	// 0. Open read pipe
	int read_fd = call_open(test_pipe_path, O_RDONLY | O_NONBLOCK, &output_buffer, &error_count);
	
	struct pollfd read_poll_arr[1];
	read_poll_arr[0].fd = read_fd;
	read_poll_arr[0].events = bitmask;

	check_sys_call(poll(read_poll_arr, 1, poll_timeout_ms), &output_buffer);
	if (read_poll_arr[0].revents != 0) {
		error_count += 1;
	}

	printf("0. open(r) = %s -> %d\n", test_pipe_path.c_str(), read_fd);
	printf("0. poll    = %s\n", str_event_list(read_poll_arr, poll_props, &output_buffer));
	printf("0. read    = %s\n", call_read(read_fd, &output_buffer, &error_count));

	// 1. Open write pipe once
	int write_fd = call_open(test_pipe_path, O_WRONLY | nonblock_flags, &output_buffer, &error_count);
	if (write_fd > 0) {
		write(write_fd, "test 1", 6);
		printf("1. open(w) = %s -> %d\n", test_pipe_path.c_str(), write_fd);
	}
	else {
		printf("1. open(w) = %s -> %s\n", test_pipe_path.c_str(), output_buffer.c_str());
		error_count += 1;
	}

	// See what the read poll says with the writer open
	check_sys_call(poll(read_poll_arr, 1, poll_timeout_ms), &output_buffer);
	if ((read_poll_arr[0].revents & POLLIN) == 0) {
		error_count += 1;
	}

	printf("1. poll    = %s\n", str_event_list(read_poll_arr, poll_props, &output_buffer));
	printf("1. read    = %s\n", call_read(read_fd, &output_buffer, &error_count));

	if (write_fd > 0) {
		close(write_fd);
	}

	// 2. See what the read poll says with the writer closed
	check_sys_call(poll(read_poll_arr, 1, poll_timeout_ms), &output_buffer);
	if (read_poll_arr[0].revents != POLLHUP) {
		error_count += 1;
	}

	printf("2. poll    = %s\n", str_event_list(read_poll_arr, poll_props, &output_buffer));
	printf("2. read    = %s\n", call_read(read_fd, &output_buffer, &error_count));

	// 3. Open write pipe a second time
	write_fd = call_open(test_pipe_path, O_WRONLY | nonblock_flags, &output_buffer, &error_count);
	if (write_fd > 0) {
		write(write_fd, "test 2", 6);
		printf("3. open(w) = %s -> %d\n", test_pipe_path.c_str(), write_fd);
	}
	else {
		printf("3. open(w) = %s -> %s\n", test_pipe_path.c_str(), output_buffer.c_str());
		error_count += 1;
	}

	// See what the read poll says with the writer open
	check_sys_call(poll(read_poll_arr, 1, poll_timeout_ms), &output_buffer);
	if ((read_poll_arr[0].revents & POLLIN) == 0) {
		error_count += 1;
	}

	printf("3. poll    = %s\n", str_event_list(read_poll_arr, poll_props, &output_buffer));
	printf("3. read    = %s\n", call_read(read_fd, &output_buffer, &error_count));

	if (write_fd > 0) {
		close(write_fd);
	}

	// 4. See what the read poll says with the writer closed
	check_sys_call(poll(read_poll_arr, 1, poll_timeout_ms), &output_buffer);
	if (read_poll_arr[0].revents != POLLHUP) {
		error_count += 1;
	}

	printf("4. poll    = %s\n", str_event_list(read_poll_arr, poll_props, &output_buffer));
	printf("4. read    = %s\n", call_read(read_fd, &output_buffer, &error_count));

	if (error_count == 0) {
		printf("PASS\n");
	}
	else {
		printf("FAIL: got %d errors\n", error_count);
	}
}

int main() {
	try {
		sub_main();
		return 0;
	}
	catch (std::string& e) {
		std::cerr << e;
		return 1;
	}
}
