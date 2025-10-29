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

// Global history vector
vector<string> commandHistory;

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
    // Only return actual shell builtins, not extended commands we implemented
    return {"echo", "exit", "type", "pwd", "cd", "history"};
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
    int historyIndex = commandHistory.size(); // Start beyond history (current line)
    string currentLine; // Store the current line being typed before history navigation

    while (read(STDIN_FILENO, &ch, 1) == 1) {
        // Handle escape sequences (arrow keys)
        if (ch == '\x1b') { // ESC character
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
            
            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Up arrow
                    if (!commandHistory.empty()) {
                        if (historyIndex == (int)commandHistory.size()) {
                            // Save current line when starting history navigation
                            currentLine = line;
                        }
                        if (historyIndex > 0) {
                            historyIndex--;
                            line = commandHistory[historyIndex];
                            
                            // Clear current line and display history entry
                            write(STDOUT_FILENO, "\r$ ", 3);
                            // Clear to end of line
                            write(STDOUT_FILENO, "\033[K", 3);
                            // Display the history entry
                            write(STDOUT_FILENO, line.c_str(), line.length());
                        }
                    }
                    continue;
                } else if (seq[1] == 'B') { // Down arrow
                    if (!commandHistory.empty()) {
                        if (historyIndex < (int)commandHistory.size() - 1) {
                            historyIndex++;
                            line = commandHistory[historyIndex];
                            
                            // Clear current line and display history entry
                            write(STDOUT_FILENO, "\r$ ", 3);
                            // Clear to end of line
                            write(STDOUT_FILENO, "\033[K", 3);
                            // Display the history entry
                            write(STDOUT_FILENO, line.c_str(), line.length());
                        } else if (historyIndex == (int)commandHistory.size() - 1) {
                            // Go back to the original current line
                            historyIndex = commandHistory.size();
                            line = currentLine;
                            
                            // Clear current line and display original line
                            write(STDOUT_FILENO, "\r$ ", 3);
                            // Clear to end of line
                            write(STDOUT_FILENO, "\033[K", 3);
                            // Display the original line
                            write(STDOUT_FILENO, line.c_str(), line.length());
                        }
                    }
                    continue;
                }
            }
        }
        
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
            historyIndex = commandHistory.size(); // Exit history navigation
            currentLine.clear();
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
            historyIndex = commandHistory.size(); // Exit history navigation
            currentLine.clear();
        } else if (ch >= 32 && ch < 127) {  // Printable characters
            line += ch;
            write(STDOUT_FILENO, &ch, 1);
            tabPressCount = 0; // reset on any other key
            historyIndex = commandHistory.size(); // Exit history navigation
            currentLine.clear();
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

        // Add command to history (except empty commands)
        if (!input.empty()) {
            commandHistory.push_back(input);
        }

        vector<string> args = parseInput(input);
        if (args.empty()) {
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        // Helper function to check if a command is builtin
        auto isBuiltin = [](const vector<string>& cmdArgs) -> bool {
            if (cmdArgs.empty()) return false;
            vector<string> builtins = getBuiltinCommands();
            return find(builtins.begin(), builtins.end(), cmdArgs[0]) != builtins.end();
        };

        // Helper function to execute builtin command
        auto executeBuiltin = [](const vector<string>& cmdArgs, int in_fd = -1, int out_fd = -1) {
            if (cmdArgs.empty()) return;
            
            string cmd = cmdArgs[0];
            int saved_stdin = -1, saved_stdout = -1;
            
            // Save original file descriptors
            if (in_fd != -1) {
                saved_stdin = dup(STDIN_FILENO);
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            if (out_fd != -1) {
                saved_stdout = dup(STDOUT_FILENO);
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
            }

            // Execute builtin
            if (cmd == "echo") {
                for (size_t i = 1; i < cmdArgs.size(); ++i) {
                    cout << cmdArgs[i]; 
                    if (i != cmdArgs.size()-1) cout << " ";
                }
                cout << "\n";
            } else if (cmd == "pwd") {
                cout << getCurrentDirectory() << "\n";
            } else if (cmd == "cd") {
                if (cmdArgs.size() < 2) {
                    const char* home = getenv("HOME");
                    if (home) {
                        if (chdir(home) != 0) {
                            cerr << "cd: " << home << ": No such file or directory\n";
                        }
                    }
                } else {
                    string path = cmdArgs[1];
                    if (path == "~") {
                        const char* home = getenv("HOME");
                        if (home) path = home;
                    }
                    if (chdir(path.c_str()) != 0) {
                        cerr << "cd: " << path << ": No such file or directory\n";
                    }
                }
            } else if (cmd == "type") {
                if (cmdArgs.size() < 2) { /* ignore */ }
                else {
                    string name = cmdArgs[1];
                    vector<string> builtins = getBuiltinCommands();
                    if (find(builtins.begin(), builtins.end(), name) != builtins.end()) {
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
            } else if (cmd == "history") {
                // Display command history with optional limit
                size_t start_index = 0;
                size_t count = commandHistory.size();
                
                if (cmdArgs.size() >= 2) {
                    // Try to parse the number argument
                    try {
                        int n = stoi(cmdArgs[1]);
                        if (n > 0) {
                            if ((size_t)n < count) {
                                start_index = count - n;
                            }
                            // If n is larger than history size, show all (start_index remains 0)
                        }
                    } catch (const exception& e) {
                        // If argument is not a valid number, ignore it and show all history
                    }
                }
                
                for (size_t i = start_index; i < count; ++i) {
                    cout << "    " << (i + 1) << "  " << commandHistory[i] << "\n";
                }
            }

            // Restore file descriptors
            if (saved_stdin != -1) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            if (saved_stdout != -1) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
        };

        // Handle multi-command pipeline
        vector<vector<string>> commands;
        vector<string> currentCmd;
        
        for (const auto& arg : args) {
            if (arg == "|") {
                if (!currentCmd.empty()) {
                    commands.push_back(currentCmd);
                    currentCmd.clear();
                }
            } else {
                currentCmd.push_back(arg);
            }
        }
        if (!currentCmd.empty()) {
            commands.push_back(currentCmd);
        }

        // If we have multiple commands, handle as pipeline
        if (commands.size() > 1) {
            int numCommands = commands.size();
            vector<pid_t> pids;
            vector<vector<int>> pipes(numCommands - 1, vector<int>(2));

            // Create all pipes
            for (int i = 0; i < numCommands - 1; i++) {
                if (pipe(pipes[i].data()) == -1) {
                    perror("pipe");
                    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
                    continue;
                }
            }

            // Execute each command
            for (int i = 0; i < numCommands; i++) {
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process
                    
                    // Set up input redirection (except for first command)
                    if (i > 0) {
                        dup2(pipes[i-1][0], STDIN_FILENO);
                    }
                    
                    // Set up output redirection (except for last command)
                    if (i < numCommands - 1) {
                        dup2(pipes[i][1], STDOUT_FILENO);
                    }
                    
                    // Close all pipe file descriptors in child
                    for (int j = 0; j < numCommands - 1; j++) {
                        close(pipes[j][0]);
                        close(pipes[j][1]);
                    }
                    
                    // Execute the command
                    if (isBuiltin(commands[i])) {
                        executeBuiltin(commands[i]);
                        exit(0);
                    } else {
                        executeCommand(commands[i]);
                        exit(1); // Should not reach here if exec succeeds
                    }
                } else if (pid > 0) {
                    pids.push_back(pid);
                } else {
                    perror("fork");
                }
            }

            // Close all pipe file descriptors in parent
            for (int i = 0; i < numCommands - 1; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }

            // Wait for all child processes
            for (pid_t pid : pids) {
                waitpid(pid, nullptr, 0);
            }

            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        // Handle single command with output redirection
        string redirectStdoutFile, redirectStderrFile;
        bool hasStdoutRedirect = false, hasStderrRedirect = false;
        bool appendStdout = false, appendStderr = false;
        vector<string> cmdArgs = commands.empty() ? args : commands[0];

        // Parse redirections for single command
        vector<string> filteredArgs;
        for (size_t i = 0; i < cmdArgs.size(); ++i) {
            if (cmdArgs[i] == ">" || cmdArgs[i] == "1>") {
                if (i + 1 < cmdArgs.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = false;
                    redirectStdoutFile = cmdArgs[i + 1];
                    i++;
                }
            } else if (cmdArgs[i] == ">>" || cmdArgs[i] == "1>>") {
                if (i + 1 < cmdArgs.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = true;
                    redirectStdoutFile = cmdArgs[i + 1];
                    i++;
                }
            } else if (cmdArgs[i] == "2>") {
                if (i + 1 < cmdArgs.size()) {
                    hasStderrRedirect = true;
                    appendStderr = false;
                    redirectStderrFile = cmdArgs[i + 1];
                    i++;
                }
            } else if (cmdArgs[i] == "2>>") {
                if (i + 1 < cmdArgs.size()) {
                    hasStderrRedirect = true;
                    appendStderr = true;
                    redirectStderrFile = cmdArgs[i + 1];
                    i++;
                }
            } else {
                filteredArgs.push_back(cmdArgs[i]);
            }
        }

        if (filteredArgs.empty()) {
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            continue;
        }

        string cmd = filteredArgs[0];

        // exit
        if (cmd == "exit" && filteredArgs.size() == 2 && filteredArgs[1] == "0") return 0;

        int saved_stdout = -1, saved_stderr = -1;

        if (hasStdoutRedirect) {
            saved_stdout = dup(STDOUT_FILENO);
            int flags = O_WRONLY | O_CREAT | (appendStdout ? O_APPEND : O_TRUNC);
            int fd = open(redirectStdoutFile.c_str(), flags, 0644);
            if (fd < 0) { 
                cerr << "Error opening file: " << redirectStdoutFile << "\n"; 
                tcsetattr(STDIN_FILENO, TCSANOW, &new_tio); 
                continue; 
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (hasStderrRedirect) {
            saved_stderr = dup(STDERR_FILENO);
            int flags = O_WRONLY | O_CREAT | (appendStderr ? O_APPEND : O_TRUNC);
            int fd = open(redirectStderrFile.c_str(), flags, 0644);
            if (fd < 0) { 
                cerr << "Error opening file: " << redirectStderrFile << "\n"; 
                if (saved_stdout>=0) {
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                } 
                tcsetattr(STDIN_FILENO, TCSANOW, &new_tio); 
                continue; 
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        // Builtins (non-pipeline)
        if (cmd == "echo") {
            for (size_t i = 1; i < filteredArgs.size(); ++i) {
                cout << filteredArgs[i]; 
                if (i != filteredArgs.size()-1) cout << " ";
            }
            cout << "\n";
        } else if (cmd == "pwd") {
            cout << getCurrentDirectory() << "\n";
        } else if (cmd == "cd") {
            if (filteredArgs.size() < 2) {
                const char* home = getenv("HOME");
                if (home) {
                    if (chdir(home) != 0) {
                        cerr << "cd: " << home << ": No such file or directory\n";
                    }
                }
            } else {
                string path = filteredArgs[1];
                if (path == "~") {
                    const char* home = getenv("HOME");
                    if (home) path = home;
                }
                if (chdir(path.c_str()) != 0) {
                    cerr << "cd: " << path << ": No such file or directory\n";
                }
            }
        } else if (cmd == "type") {
            if (filteredArgs.size() < 2) { /* ignore */ }
            else {
                string name = filteredArgs[1];
                vector<string> builtins = getBuiltinCommands();
                if (find(builtins.begin(), builtins.end(), name) != builtins.end()) {
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
        } else if (cmd == "history") {
            // Display command history with optional limit
            size_t start_index = 0;
            size_t count = commandHistory.size();
            
            if (filteredArgs.size() >= 2) {
                // Try to parse the number argument
                try {
                    int n = stoi(filteredArgs[1]);
                    if (n > 0) {
                        if ((size_t)n < count) {
                            start_index = count - n;
                        }
                        // If n is larger than history size, show all (start_index remains 0)
                    }
                } catch (const exception& e) {
                    // If argument is not a valid number, ignore it and show all history
                }
            }
            
            for (size_t i = start_index; i < count; ++i) {
                cout << "    " << (i + 1) << "  " << commandHistory[i] << "\n";
            }
        } else {
            // For non-builtin commands like "cat", execute normally
            executeCommand(filteredArgs);
        }

        // Restore stdout/stderr
        if (saved_stdout >= 0) { 
            dup2(saved_stdout, STDOUT_FILENO); 
            close(saved_stdout); 
        }
        if (saved_stderr >= 0) { 
            dup2(saved_stderr, STDERR_FILENO); 
            close(saved_stderr); 
        }

        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    return 0;
}