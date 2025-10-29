#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <cstdlib>
#include <fstream>

using namespace std;

// Helper function to parse quoted and unquoted words
vector<string> parseInput(const string &input) {
    vector<string> args;
    string current;
    bool inQuotes = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if ((c == '\'' || c == '"')) {
            if (!inQuotes) {
                inQuotes = true;
                quoteChar = c;
            } else if (quoteChar == c) {
                inQuotes = false;
            } else {
                current += c;
            }
        } else if (isspace(c) && !inQuotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

string getCurrentDirectory() {
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) != NULL)
        return string(buffer);
    else
        return "";
}

string findInPath(const string& program) {
    const char* path = getenv("PATH");
    if (!path) return "";
    
    string pathStr(path);
    stringstream ss(pathStr);
    string dir;
    
    while (getline(ss, dir, ':')) {
        string fullPath = dir + "/" + program;
        if (access(fullPath.c_str(), X_OK) == 0) {
            return fullPath;
        }
    }
    return "";
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    while (true) {
        cout << "$ ";
        string input;
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        vector<string> args = parseInput(input);
        if (args.empty()) continue;

        string cmd = args[0];

        // exit
        if (cmd == "exit" && args.size() == 2 && args[1] == "0") return 0;

        // echo
        else if (cmd == "echo") {
            for (size_t i = 1; i < args.size(); ++i) {
                cout << args[i];
                if (i != args.size() - 1) cout << " ";
            }
            cout << "\n";
        }

        // pwd
        else if (cmd == "pwd") {
            cout << getCurrentDirectory() << "\n";
        }

        // cd
        else if (cmd == "cd") {
            if (args.size() < 2) continue;
            string path = args[1];
            if (path == "~") {
                const char* home = getenv("HOME");
                if (home) path = home;
                else path = "/home/user";
            }
            if (chdir(path.c_str()) != 0) {
                cerr << "cd: " << path << ": No such file or directory\n";
            }
        }

        // type
        else if (cmd == "type") {
            if (args.size() < 2) continue;
            string name = args[1];
            if (name == "echo" || name == "exit" || name == "type" || name == "pwd" || name == "cd") {
                cout << name << " is a shell builtin\n";
            } else {
                string path = findInPath(name);
                if (!path.empty()) {
                    cout << name << " is " << path << "\n";
                } else {
                    cout << name << ": not found\n";
                }
            }
        }

        // cat
        else if (cmd == "cat") {
            if (args.size() < 2) continue;
            for (size_t i = 1; i < args.size(); ++i) {
                ifstream file(args[i].c_str());
                if (!file.is_open()) {
                    cerr << "cat: " << args[i] << ": No such file or directory\n";
                    continue;
                }
                string line;
                while (getline(file, line)) {
                    cout << line;
                }
                file.close();
            }
            cout << "\n";
        }

        else {
            // Try to execute as external program
            string programPath = findInPath(cmd);
            
            if (programPath.empty()) {
                cout << cmd << ": command not found\n";
                continue;
            }
            
            // Execute the program
            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                vector<char*> execArgs;
                for (size_t i = 0; i < args.size(); ++i) {
                    execArgs.push_back(const_cast<char*>(args[i].c_str()));
                }
                execArgs.push_back(nullptr);
                
                execv(programPath.c_str(), execArgs.data());
                exit(1); // If execv fails
            } else if (pid > 0) {
                // Parent process
                int status;
                waitpid(pid, &status, 0);
            }
        }
    }

    return 0;
}