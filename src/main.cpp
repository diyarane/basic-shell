#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <termios.h>
#include <dirent.h>

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

vector<string> getBuiltinCommands() {
    return {"echo", "exit", "type", "pwd", "cd"};
}

vector<string> getExecutablesInPath(const string& prefix) {
    vector<string> executables;
    const char* path = getenv("PATH");
    if (!path) return executables;
    
    string pathStr(path);
    stringstream ss(pathStr);
    string dir;
    
    while (getline(ss, dir, ':')) {
        // Skip empty directories
        if (dir.empty()) continue;
        
        // Open directory - skip if it doesn't exist
        DIR* dirp = opendir(dir.c_str());
        if (!dirp) continue;
        
        struct dirent* entry;
        while ((entry = readdir(dirp)) != nullptr) {
            string filename = entry->d_name;
            // Skip . and ..
            if (filename == "." || filename == "..") continue;
            
            // Check if filename starts with prefix
            if (filename.find(prefix) == 0) {
                string fullPath = dir + "/" + filename;
                // Check if it's executable
                if (access(fullPath.c_str(), X_OK) == 0) {
                    // Avoid duplicates
                    if (find(executables.begin(), executables.end(), filename) == executables.end()) {
                        executables.push_back(filename);
                    }
                }
            }
        }
        closedir(dirp);
    }
    
    return executables;
}

vector<string> findCompletions(const string& prefix) {
    vector<string> completions;
    
    // If prefix is empty, don't complete
    if (prefix.empty()) return completions;
    
    vector<string> builtins = getBuiltinCommands();
    
    // Add matching builtins first
    for (const auto& builtin : builtins) {
        if (builtin.find(prefix) == 0) {  // starts with prefix
            completions.push_back(builtin);
        }
    }
    
    // Add matching executables from PATH
    vector<string> executables = getExecutablesInPath(prefix);
    for (const auto& exe : executables) {
        // Avoid duplicates with builtins
        if (find(completions.begin(), completions.end(), exe) == completions.end()) {
            completions.push_back(exe);
        }
    }
    
    return completions;
}

string findCommonPrefix(const vector<string>& strings) {
    if (strings.empty()) return "";
    if (strings.size() == 1) return strings[0];
    
    string prefix = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        size_t j = 0;
        while (j < prefix.length() && j < strings[i].length() && prefix[j] == strings[i][j]) {
            ++j;
        }
        prefix = prefix.substr(0, j);
        if (prefix.empty()) break;
    }
    
    return prefix;
}

string readLineWithCompletion() {
    string line;
    char ch;
    int tabPressCount = 0; // Track consecutive TAB presses

    while (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == '\n') {
            write(STDOUT_FILENO, "\n", 1);
            tabPressCount = 0; // Reset on Enter
            break;
        } else if (ch == 127 || ch == 8) {  // Backspace
            if (!line.empty()) {
                line.pop_back();
                write(STDOUT_FILENO, "\b \b", 3);
            }
            tabPressCount = 0; // Reset on any other key
        } else if (ch == '\t') {  // Tab completion
            size_t lastSpace = line.find_last_of(' ');
            string currentWord = (lastSpace == string::npos) ? line : line.substr(lastSpace + 1);

            // Only complete the first word (the command)
            if (lastSpace == string::npos && !currentWord.empty()) {
                vector<string> completions = findCompletions(currentWord);

                if (completions.empty()) {
                    write(STDOUT_FILENO, "\a", 1); // No matches, ring bell
                    tabPressCount = 0;
                } else if (completions.size() == 1) {
                    // Unique match - complete immediately
                    string completion = completions[0];
                    string toAdd = completion.substr(currentWord.size()) + " ";
                    line += toAdd;
                    write(STDOUT_FILENO, toAdd.c_str(), toAdd.size());
                    tabPressCount = 0;
                } else {
                    // Multiple matches: complete to longest common prefix
                    string lcp = findCommonPrefix(completions);
                    if (lcp.size() > currentWord.size()) {
                        string toAdd = lcp.substr(currentWord.size());
                        line += toAdd;
                        write(STDOUT_FILENO, toAdd.c_str(), toAdd.size());
                    } else {
                        // If no further completion possible, ring bell
                        write(STDOUT_FILENO, "\a", 1);
                    }

                    tabPressCount++;
                    if (tabPressCount == 2) {
                        // Second TAB: list matches alphabetically
                        sort(completions.begin(), completions.end());
                        string output = "\n";
                        for (const auto& comp : completions) {
                            output += comp + "  ";
                        }
                        output += "\n$ " + line;
                        write(STDOUT_FILENO, output.c_str(), output.size());
                        tabPressCount = 0;
                    }
                }
            } else {
                // If typing arguments, ignore TAB
                tabPressCount = 0;
            }
        } else if (ch >= 32 && ch < 127) {  // Printable characters
            line += ch;
            write(STDOUT_FILENO, &ch, 1);
            tabPressCount = 0; // reset on any other key
        } else {
            tabPressCount = 0; // reset on any other key
        }
    }

    return line;
}

void executeCommand(const vector<string>& cmdArgs) {
    if (cmdArgs.empty()) return;

    string cmd = cmdArgs[0];
    string path = findInPath(cmd);
    if (path.empty()) {
        cout << cmd << ": command not found\n";
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        vector<char*> execArgs;
        for (const auto& arg : cmdArgs) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        execArgs.push_back(nullptr);

        execv(path.c_str(), execArgs.data());
        perror("execv failed");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork failed");
    }
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;

    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    while (true) {
        cout << "$ ";
        string input = readLineWithCompletion();

        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

        if (input.empty()) {
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        vector<string> args = parseInput(input);
        if (args.empty()) {
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        // Handle dual-command pipeline
        auto pipePos = find(args.begin(), args.end(), "|");
        if (pipePos != args.end()) {
            vector<string> cmd1(args.begin(), pipePos);
            vector<string> cmd2(pipePos + 1, args.end());

            int fd[2];
            if (pipe(fd) == -1) {
                perror("pipe");
                tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
                continue;
            }

            pid_t pid1 = fork();
            if (pid1 == 0) {
                close(fd[0]);
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
                executeCommand(cmd1);
                exit(0);
            }

            pid_t pid2 = fork();
            if (pid2 == 0) {
                close(fd[1]);
                dup2(fd[0], STDIN_FILENO);
                close(fd[0]);
                executeCommand(cmd2);
                exit(0);
            }

            close(fd[0]);
            close(fd[1]);
            waitpid(pid1, nullptr, 0);
            waitpid(pid2, nullptr, 0);
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        // Handle output redirection
        string redirectStdoutFile, redirectStderrFile;
        bool hasStdoutRedirect = false, hasStderrRedirect = false;
        bool appendStdout = false, appendStderr = false;
        vector<string> cmdArgs;

        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") {
                if (i + 1 < args.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = false;
                    redirectStdoutFile = args[i + 1];
                    i++;
                }
            } else if (args[i] == ">>" || args[i] == "1>>") {
                if (i + 1 < args.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = true;
                    redirectStdoutFile = args[i + 1];
                    i++;
                }
            } else if (args[i] == "2>") {
                if (i + 1 < args.size()) {
                    hasStderrRedirect = true;
                    appendStderr = false;
                    redirectStderrFile = args[i + 1];
                    i++;
                }
            } else if (args[i] == "2>>") {
                if (i + 1 < args.size()) {
                    hasStderrRedirect = true;
                    appendStderr = true;
                    redirectStderrFile = args[i + 1];
                    i++;
                }
            } else {
                cmdArgs.push_back(args[i]);
            }
        }

        if (cmdArgs.empty()) {
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        string cmd = cmdArgs[0];

        // exit
        if (cmd == "exit" && cmdArgs.size() == 2 && cmdArgs[1] == "0") return 0;

        int saved_stdout = -1, saved_stderr = -1;

        if (hasStdoutRedirect) {
            saved_stdout = dup(STDOUT_FILENO);
            int flags = O_WRONLY | O_CREAT | (appendStdout ? O_APPEND : O_TRUNC);
            int fd = open(redirectStdoutFile.c_str(), flags, 0644);
            if (fd < 0) { cerr << "Error opening file: " << redirectStdoutFile << "\n"; tcsetattr(STDIN_FILENO, TCSANOW, &new_tio); continue; }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (hasStderrRedirect) {
            saved_stderr = dup(STDERR_FILENO);
            int flags = O_WRONLY | O_CREAT | (appendStderr ? O_APPEND : O_TRUNC);
            int fd = open(redirectStderrFile.c_str(), flags, 0644);
            if (fd < 0) { cerr << "Error opening file: " << redirectStderrFile << "\n"; if (saved_stdout>=0){dup2(saved_stdout,STDOUT_FILENO);close(saved_stdout);} tcsetattr(STDIN_FILENO, TCSANOW,&new_tio); continue; }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        // Builtins
        if (cmd == "echo") {
            for (size_t i = 1; i < cmdArgs.size(); ++i) {
                cout << cmdArgs[i]; if (i != cmdArgs.size()-1) cout << " ";
            }
            cout << "\n";
        } else if (cmd == "pwd") {
            cout << getCurrentDirectory() << "\n";
        } // cd
        else if (cmd == "cd") {
            if (cmdArgs.size() < 2) {
                // No argument: default to HOME
                const char* home = getenv("HOME");
                if (home) {
                    if (chdir(home) != 0) {
                        cerr << "cd: " << home << ": No such file or directory\n";
                    }
                }
                continue;
            }

            string path = cmdArgs[1];
            if (path == "~") {
                const char* home = getenv("HOME");
                if (home) path = home;
                // If HOME is not set, just leave path as "~" so chdir will fail
            }

            if (chdir(path.c_str()) != 0) {
                cerr << "cd: " << path << ": No such file or directory\n";
            }
        }
        else if (cmd == "type") {
            if (cmdArgs.size() < 2) { /* ignore */ }
            else {
                string name = cmdArgs[1];
                
                // First check if it's a builtin command
                vector<string> builtins = getBuiltinCommands();
                if (find(builtins.begin(), builtins.end(), name) != builtins.end()) {
                    cout << name << " is a shell builtin\n";
                } 
                // If not builtin, check if it's in PATH
                else {
                    string path = findInPath(name);
                    if (!path.empty()) {
                        cout << name << " is " << path << "\n";
                    } else {
                        cout << name << ": not found\n";
                    }
                }
            }
        } 
        // cat
        else if (cmd == "cat") {
            if (cmdArgs.size() < 2) {
                if (hasStdoutRedirect && saved_stdout >= 0) {
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                }
                if (hasStderrRedirect && saved_stderr >= 0) {
                    dup2(saved_stderr, STDERR_FILENO);
                    close(saved_stderr);
                }
                tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
                continue;
            }

            for (size_t i = 1; i < cmdArgs.size(); ++i) {
                ifstream file(cmdArgs[i].c_str(), ios::binary);
                if (!file.is_open()) {
                    cerr << "cat: " << cmdArgs[i] << ": No such file or directory\n";
                    continue;
                }
                cout << file.rdbuf(); // stream entire file content directly
                file.close();
            }
        }
        else {
            executeCommand(cmdArgs);
        }

        // Restore stdout/stderr
        if (saved_stdout >= 0) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
        if (saved_stderr >= 0) { dup2(saved_stderr, STDERR_FILENO); close(saved_stderr); }

        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    return 0;
}