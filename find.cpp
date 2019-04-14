#include <iostream>
#include <vector>
#include <queue>
#include <dirent.h>
#include <sys/stat.h>
#include <wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>

#define NOSIZE INT64_MAX

static const std::string help = "Usage:"
                                "\n\tpath [options] - find files by path with options."
                                "\n\n\tOptions:"
                                "\n\t\t-inum inum - inode number;"
                                "\n\t\t-name name - file name;"
                                "\n\t\t-size [-=+]size - file's size(less, equal, more);"
                                "\n\t\t-nlinks num - file's hardlinks;"
                                "\n\t\t-exec path - file to execute;\n";


struct usage {

    std::string path;

    int needs_inum = 0;
    ino_t inum;

    int needs_name = 0;
    std::string name;

    int64_t size_sign = NOSIZE;
    off_t size;

    int needs_nlinks = 0;
    nlink_t nlinks;

    int needs_exec = 0;
    std::string exec_path;
};

static bool ok = true;
static std::string reason;

usage parse_arguments(int& argc, char* argv[]) {
    usage arguments;
    arguments.path = argv[1];
    for (int i = 2; i < argc; i += 2) {
        std::string option(argv[i]);
        std::string value(argv[i + 1]);
        if (option == "-inum") {
            arguments.needs_inum = 1;
            arguments.inum = std::stoul(value);
        } else if (option == "-name") {
            arguments.needs_name = 1;
            arguments.name = value;
        } else if (option == "-size") {
            char sign = value[0];
            int64_t size_sign;
            switch (sign) {
                case '-':
                    size_sign = -1;
                    break;
                case '=':
                    size_sign = 0;
                    break;
                case '+':
                    size_sign = 1;
                    break;
                default:
                    ok = false;
                    reason = "Wrong usage of \"size\" option";
                    return arguments;
            }
            arguments.size_sign = size_sign;
            arguments.size = std::stoul(value.substr(1));
        } else if (option == "-nlinks") {
            arguments.needs_nlinks = 1;
            arguments.nlinks = std::stoul(value);
        } else if (option == "-exec") {
            arguments.needs_exec = 1;
            arguments.exec_path = value;
        } else {
            ok = false;
            reason = "Unknown option";
            return arguments;
        }
    }
    return arguments;
}

bool check(const usage& arguments, const struct stat& sb, const std::string& name) {
    bool satisfy = true;
    if (arguments.needs_nlinks) {
        satisfy &= (arguments.nlinks == sb.st_nlink);
    }
    if (arguments.needs_name) {
        satisfy &= (arguments.name == name);
    }
    if (arguments.needs_inum) {
        satisfy &= (arguments.inum == sb.st_ino);
    }
    if (arguments.size_sign != NOSIZE) {
        switch (arguments.size_sign) {
            case -1:
                satisfy &= (sb.st_size < arguments.size);
                break;
            case 0:
                satisfy &= (sb.st_size == arguments.size);
                break;
            case 1:
                satisfy &= (sb.st_size > arguments.size);
                break;
            default:
                break;
        }
    }
    return satisfy;
}

void find(const usage& arguments, std::vector<std::string>& result) {
    std::queue<std::string> queue;
    queue.push(arguments.path);
    while (!queue.empty()) {
        std::string cur_dir = queue.front();
        queue.pop();
        DIR* dir = opendir(cur_dir.data());
        if (dir == nullptr) {
            perror("opendir");
            continue;
        }
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            auto relative_path = entry->d_name;
            if (!relative_path || strcmp(relative_path, ".") == 0 || strcmp(relative_path, "..") == 0) {
                continue;
            }
            auto path = cur_dir + "/" + relative_path;
            struct stat sb;
            int ret = lstat(path.data(), &sb);
            if (ret == -1) {
                perror("lstat");
                continue;
            }

            if (S_ISDIR(sb.st_mode)) {
                queue.push(path);
            } else {
                if (check(arguments, sb, relative_path)) {
                    result.push_back(path);
                }
            }
        }
        closedir(dir);
    }
}

void execute(std::string& filepath, std::vector<std::string>& result) {
    int status;
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid == 0) {
        std::vector<char *> args;
        args.reserve(result.size());
        for (auto& file : result) {
            args.push_back(&(file[0]));
        }
        args.emplace_back(nullptr);

        int err = execv(filepath.data(), args.data());
        if (err == -1) {
            perror("execv");
            exit(EXIT_FAILURE);
        }
    } else {
        do {
            pid_t wait_pid = waitpid(pid, &status, WUNTRACED | WCONTINUED);
            if (wait_pid == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }

            if (WIFEXITED(status)) {
                printf("Normal exited, status = %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("Was killed by signal %d\n", WTERMSIG(status));
            } else if (WIFSTOPPED(status)) {
                printf("Was stopped by signal %d\n", WSTOPSIG(status));
            } else if (WIFCONTINUED(status)) {
                printf("Was continued\n");
            }
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

int main(int argc, char* argv[]) {
    std::cout << help << std::endl;
    if (argc <= 1) {
        std::cout << "Wrong usage, see help" << std::endl;
    }
    usage arguments = parse_arguments(argc, argv);
    if (!ok) {
        std::cerr << reason << ", see help" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::vector<std::string> result;
    find(arguments, result);

    for (const std::string& file : result) {
        std::cout << file << std::endl;
    }

    if (arguments.needs_exec) {
        execute(arguments.exec_path, result);
    }

    return 0;
}