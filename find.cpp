#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <iostream>
#include <sys/stat.h>
#include <cstring>
#include <limits>

const int MAX_ARGS = 1024;
const int MAX_RETRIES = 100;

char *parsed[MAX_ARGS];
char *env[MAX_ARGS];
std::vector<std::string> files;
bool filter_inum, filter_name, filter_exec, filter_nlinks, filter_size;
int type_size;
off_t size;
nlink_t nlinks;
ino_t inum;
std::string name;

void out_stat_errors() {
    if (errno == ENAMETOOLONG) {
        fprintf(stderr,"Pathname is too long: %s\n", strerror(errno));
    } else if (errno == ENOENT) {
        fprintf(stderr,"A component of pathname does not exist, or pathname is an empty string: %s\n", strerror(errno));
    } else if (errno == ENOTDIR) {
        fprintf(stderr,"A component of the path prefix of pathname is not a directory: %s\n", strerror(errno));
    } else if (errno == EFAULT) {
        fprintf(stderr,"Bad adress: %s\n", strerror(errno));
    } else if (errno == EACCES) {
        //ignoring files and directories to which we don't have access
        return;
    } else {
        fprintf(stderr,"Something strange has happened with file: %s\n", strerror(errno));
    }
    exit(0);
}

void check(std::string file_path, const std::string &file_name) {
    struct stat buf{};
    int status = stat(file_path.c_str(), &buf);
    if (status == -1) {
        out_stat_errors();
        return;
    }
    if ((!filter_inum || buf.st_ino == inum) && (!filter_name || file_name == name)
        && (!filter_nlinks || buf.st_nlink == nlinks)
        && (!filter_size || (type_size == 0 && buf.st_size < size) || (type_size == 1 && buf.st_size == size)
            || (type_size == 2 && buf.st_size > size))) {
                files.emplace_back(file_path);
    }
}

void dfs(const std::string &dir_name) {
    DIR *directory = opendir(dir_name.c_str());
    if (directory == nullptr) {
        if (errno == EACCES) {
            //Ignoring files and dirs to which we don't have access
            return;
        } else if (errno == ENOENT) {
            fprintf(stderr,"Directory does not exist, or dir_name is an empty string: %s\n", strerror(errno));
        } else if (errno == ENOTDIR) {
            fprintf(stderr,"dir_name is not a directory: %s\n", strerror(errno));
        } else if (errno == ENOMEM) {
            fprintf(stderr,"Insufficient memory to complete the operation: %s\n", strerror(errno));
        } else {
            fprintf(stderr,"Something strange happened while openning: %s\n", strerror(errno));
        }
        exit(0);
    }
    while (true) {
        errno = 0;
        dirent *v = readdir(directory);
        if (v == nullptr) {
            if (errno == EBADF) {
                fprintf(stderr,"Something strange happened with directory: %s\n", strerror(errno));
                exit(0);
            }
            break;
        }
        std::string file_path = dir_name + "/" + std::string(v->d_name);
        if (v->d_type == DT_REG) {
            check(file_path, std::string(v->d_name));
        }
        if (v->d_type == DT_DIR && std::string(v->d_name) != "." && std::string(v->d_name) != "..") {
            dfs(file_path);
        }
    }
    int status = closedir(directory);
    if (status == -1) {
        fprintf(stderr,"Something strange happened with directory: %s\n", strerror(errno));
        exit(0);
    }
}

void print_error_invalid_args(bool exp) {
    if (exp) {
        fprintf(stderr,"Invalid arguments!\n");
        exit(0);
    }
}

void del_args() {
    for (int i = 1; parsed[i] != nullptr; i++) {
        delete[] parsed[i];
    }
}

int64_t parse_number(const char *s) {
    int pos = 0;
    int kp = 1;
    int64_t n = 0;
    if (s[0] == '-') {
        kp = -1;
        pos = 1;
    }
    if (s[pos] == '\0') {
        print_error_invalid_args(true);
    }
    for (; s[pos] != '\0'; pos++) {
        if (s[pos] < '0' || s[pos] > '9') {
            print_error_invalid_args(true);
        }
        if (n > std::numeric_limits<int64_t>::max() / 10) {
            print_error_invalid_args(true);
        }
        n = 10 * n;
        if (n > std::numeric_limits<int64_t>::max() - (s[pos] - '0')) {
            if (kp == -1 && n == std::numeric_limits<int64_t>::max() - (s[pos] - '0') + 1 && s[pos + 1] == '\0') {
                n *= kp;
                n -= (s[pos] - '0');
                kp = 1;
                break;
            } else {
                print_error_invalid_args(true);
            }
        }
        n += (s[pos] - '0');
    }
    n *= kp;
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc % 2) {
        fprintf(stderr,"Wrong number of arguments!\n");
        return 0;
    }
    for (int i = 2; i < argc; i += 2) {
        std::string arg = std::string(argv[i]);
        if (arg == "-exec") {
            print_error_invalid_args(filter_exec);
            filter_exec = true;
            parsed[0] = argv[i + 1];
        } else if (arg == "-inum") {
            print_error_invalid_args(filter_inum);
            int64_t num = parse_number(argv[i + 1]);
            if (std::numeric_limits<ino_t>::min() > num || std::numeric_limits<ino_t>::max() < num) {
                print_error_invalid_args(true);
            }
            inum = static_cast<ino_t>(num);
            filter_inum = true;
        } else if (arg == "-name") {
            print_error_invalid_args(filter_name);
            filter_name = true;
            name = std::string(argv[i + 1]);
        } else if (arg == "-nlinks") {
            print_error_invalid_args(filter_nlinks);
            int64_t num = parse_number(argv[i + 1]);
            if (std::numeric_limits<nlink_t>::min() > num || std::numeric_limits<nlink_t>::max() < num) {
                print_error_invalid_args(true);
            }
            nlinks = static_cast<nlink_t>(num);
            filter_nlinks = true;
        } else if (arg == "-size") {
            print_error_invalid_args(filter_size);
            if (argv[i + 1][0] == '-') {
                type_size = 0;
            } else if (argv[i + 1][0] == '=') {
                type_size = 1;
            } else if (argv[i + 1][0] == '+') {
                type_size = 2;
            } else {
                print_error_invalid_args(true);
            }
            int64_t num = parse_number(argv[i + 1] + 1);
            if (std::numeric_limits<off_t>::min() > num || std::numeric_limits<off_t>::max() < num) {
                print_error_invalid_args(true);
            }
            size = num;
            filter_size = true;
        }
    }
    struct stat buf{};
    int error_status = stat(argv[1], &buf);
    if (error_status == -1) {
        out_stat_errors();
    } else if (buf.st_mode & S_IFREG) {
        std::string file_name;
        for (int i = 0; argv[1][i] != '\0'; i++) {
            if (argv[1][i] == '/') {
                file_name = "";
                continue;
            }
            file_name += argv[1][i];
        }
        check(std::string(argv[1]), file_name);
    } else {
        dfs(std::string(argv[1]));
    }
    if (filter_exec) {
        int pos = 1;
        for (const auto &s : files) {
            parsed[pos] = new char[s.length() + 1];
            std::strcpy(parsed[pos], s.c_str());
            pos++;
            if (pos == MAX_ARGS) {
                parsed[pos] = nullptr;
                fprintf(stderr,"Too many arguments\n");
                del_args();
                return 0;
            }
        }
        parsed[pos] = nullptr;
        pid_t child_pid;
        int tries = 0;
        do {
            child_pid = fork();
            tries++;
        } while (child_pid == -1 && errno == EAGAIN && tries <= MAX_RETRIES);
        if (child_pid == -1) {
            fprintf(stderr,"Can't fork process: %s\n", strerror(errno));
            del_args();
            return 0;
        }
        if (child_pid == 0) {
            int status;
            tries = 0;
            env[0] = nullptr;
            do {
                status = execve(parsed[0], parsed, env);
                tries++;
            } while (status == -1 && errno == EAGAIN && tries <= MAX_RETRIES);
            if (status == -1) {
                fprintf(stderr,"Errors while execution: %s\n", strerror(errno));
            }
            del_args();
            return 0;
        } else {
            int status;
            int result;
            do {
                result = waitpid(child_pid, &status, 0);
            } while (result == -1 && errno == EINTR);
            if (result == -1) {
                fprintf(stderr,"Can't wait for the result of child: %s\n", strerror(errno));
            } else if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code == 0)
                    fprintf(stdout,"Process finished with exit code %d\n", exit_code);
                else
                    fprintf(stderr,"Process finished with exit code %d\n", exit_code);
            } else if (WIFSIGNALED(status)) {
                int term_signal = WTERMSIG(status);
                fprintf(stderr,"Process terminated with signal %s\n", strsignal(term_signal));
            } else {
                fprintf(stderr,"Something strange happened with process\n");
            }
        }
        del_args();
    } else {
        for (const auto &v : files) {
            fprintf(stdout,"%s\n", v.c_str());
        }
    }
    return 0;
}