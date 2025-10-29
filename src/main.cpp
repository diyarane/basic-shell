#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

using namespace std;

// Helper function to parse quoted and unquoted words
vector<string> parseInput(const string &input) {
    vector<string> args;
    string current;
    bool inQuotes = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        
        // Handle backslash escaping
        if (c == '\\' && i + 1 < input.size()) {
            if (!inQuotes) {
                // Outside quotes: backslash escapes the next character
                current += input[i + 1];
                ++i;
                continue;
            } else if (inQuotes && quoteChar == '"') {
                // Inside double quotes: backslash escapes ", \, and $
                char next = input[i + 1];
                if (next == '"' || next == '\\' || next == '$') {
                    current += next;
                    ++i;
                    continue;
                }
                // Otherwise keep both the backslash and the next character
                current += c;
                current += input[i + 1];
                ++i;
                continue;
            } else {
                // Inside single quotes: backslash is literal
                current += c;
                continue;
            }
        }
        
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

        // Check for output redirection (> or 1>)
        string redirectFile;
        vector<string> cmdArgs;
        bool hasRedirect = false;
        
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") {
                if (i + 1 < args.size()) {
                    hasRedirect = true;
                    redirectFile = args[i + 1];
                    i++; // Skip the filename
                }
            } else {
                cmdArgs.push_back(args[i]);
            }
        }
        
        if (cmdArgs.empty()) continue;
        string cmd = cmdArgs[0];

        // exit
        if (cmd == "exit" && cmdArgs.size() == 2 && cmdArgs[1] == "0") return 0;

        // Set up output redirection if needed
        int saved_stdout = -1;
        if (hasRedirect) {
            saved_stdout = dup(STDOUT_FILENO);
            int fd = open(redirectFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                cerr << "Error opening file: " << redirectFile << "\n";
                continue;
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        // echo
        if (cmd == "echo") {
            for (size_t i = 1; i < cmdArgs.size(); ++i) {
                cout << cmdArgs[i];
                if (i != cmdArgs.size() - 1) cout << " ";
            }
            cout << "\n";
        }

        // pwd
        else if (cmd == "pwd") {
            cout << getCurrentDirectory() << "\n";
        }

        // cd
        else if (cmd == "cd") {
            if (cmdArgs.size() < 2) {
                if (hasRedirect && saved_stdout >= 0) {
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                }
                continue;
            }
            string path = cmdArgs[1];
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
            if (cmdArgs.size() < 2) {
                if (hasRedirect && saved_stdout >= 0) {
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                }
                continue;
            }
            string name = cmdArgs[1];
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
            if (cmdArgs.size() < 2) {
                if (hasRedirect && saved_stdout >= 0) {
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                }
                continue;
            }
            for (size_t i = 1; i < cmdArgs.size(); ++i) {
                ifstream file(cmdArgs[i].c_str());
                if (!file.is_open()) {
                    cerr << "cat: " << cmdArgs[i] << ": No such file or directory\n";
                    continue;
                }
                string line;
                while (getline(file, line)) {
                    cout << line;
                    if (!file.eof()) {
                        cout << "\n";
                    }
                }
                file.close();
            }
        }

        else {
            // Try to execute as external program
            string programPath = findInPath(cmd);
            
            if (programPath.empty()) {
                cout << cmd << ": command not found\n";
            } else {
                // Execute the program
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process
                    vector<char*> execArgs;
                    for (size_t i = 0; i < cmdArgs.size(); ++i) {
                        execArgs.push_back(const_cast<char*>(cmdArgs[i].c_str()));
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
        
        // Restore stdout if it was redirected
        if (hasRedirect && saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
    }

    return 0;
}