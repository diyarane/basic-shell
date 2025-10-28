#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>   // fork(), execvp(), access(), getcwd(), chdir()
#include <sys/wait.h> // waitpid()
#include <cstdlib>    // getenv()
#include <cstring>    // strerror()

using namespace std;

bool is_builtin(const string &cmd) {
    return (cmd == "echo" || cmd == "exit" || cmd == "type" || cmd == "pwd" || cmd == "cd");
}

string find_in_path(const string &cmd) {
    if (access(cmd.c_str(), X_OK) == 0) return cmd; // executable in current dir

    char *path_env = getenv("PATH");
    if (!path_env) return "";

    string path(path_env);
    stringstream ss(path);
    string dir;
    while (getline(ss, dir, ':')) {
        string full_path = dir + "/" + cmd;
        if (access(full_path.c_str(), X_OK) == 0)
            return full_path;
    }
    return "";
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    while (true) {
        cout << "$ ";
        string input;
        getline(cin, input);

        // split input into words
        vector<string> parts;
        string word;
        stringstream ss(input);
        while (ss >> word) parts.push_back(word);
        if (parts.empty()) continue;

        if (input == "exit 0") return 0;

        // echo
        else if (parts[0] == "echo") {
            for (size_t i = 1; i < parts.size(); ++i)
                cout << parts[i] << (i + 1 < parts.size() ? " " : "\n");
        }

        // pwd
        else if (parts[0] == "pwd") {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
                cout << cwd << "\n";
            else
                perror("pwd");
        }

        // cd
        else if (parts[0] == "cd") {
            if (parts.size() < 2) {
                const char* home = getenv("HOME");
                if (home == NULL) home = "/";
                if (chdir(home) != 0)
                    cerr << "cd: " << home << ": " << strerror(errno) << "\n";
            } else {
                if (chdir(parts[1].c_str()) != 0)
                    cerr << "cd: " << parts[1] << ": " << strerror(errno) << "\n";
            }
        }

        // type
        else if (parts[0] == "type") {
            if (parts.size() < 2) {
                cout << "type: missing argument\n";
                continue;
            }
            string cmd = parts[1];
            if (is_builtin(cmd))
                cout << cmd << " is a shell builtin\n";
            else {
                string path = find_in_path(cmd);
                if (!path.empty())
                    cout << cmd << " is " << path << "\n";
                else
                    cout << cmd << ": not found\n";
            }
        }

        // external commands
        else {
            string path = find_in_path(parts[0]);
            if (path.empty()) {
                cout << parts[0] << ": command not found\n";
                continue;
            }

            vector<char*> argv;
            for (auto &p : parts)
                argv.push_back(&p[0]);
            argv.push_back(NULL);

            pid_t pid = fork();
            if (pid == 0) {
                execvp(argv[0], argv.data());
                perror("execvp failed");
                exit(1);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
            } else {
                perror("fork failed");
            }
        }
    }

    return 0;
}
