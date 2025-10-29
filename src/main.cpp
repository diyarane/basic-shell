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
    return {"echo", "exit", "type", "pwd", "cd", "cat"};
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
    
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == '\n') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        } else if (ch == 127 || ch == 8) {  // Backspace
            if (!line.empty()) {
                line.pop_back();
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if (ch == '\t') {  // Tab completion
            // Find the current word being typed (last word after last space)
            size_t lastSpace = line.find_last_of(' ');
            string currentWord = (lastSpace == string::npos) ? line : line.substr(lastSpace + 1);
            
            // Only try to complete if we're at the beginning (no space yet) - completing the command
            if (lastSpace == string::npos && !currentWord.empty()) {
                vector<string> completions = findCompletions(currentWord);
                
                if (completions.size() == 1) {
                    // Unique match - complete it and add a space
                    string completion = completions[0];
                    line = completion + " ";
                    // Clear the line and re-print with completion
                    string output = "\r$ " + line;
                    write(STDOUT_FILENO, output.c_str(), output.length());
                } else if (completions.size() > 1) {
                    // Multiple matches - find common prefix
                    string commonPrefix = findCommonPrefix(completions);
                    
                    if (commonPrefix.length() > currentWord.length()) {
                        // There's a common prefix longer than what's typed - complete to it
                        line = commonPrefix;
                        string output = "\r$ " + line;
                        write(STDOUT_FILENO, output.c_str(), output.length());
                    } else {
                        // No additional common prefix - show all options
                        string output = "\n";
                        for (const auto& comp : completions) {
                            output += comp + "  ";
                        }
                        output += "\n$ " + line;
                        write(STDOUT_FILENO, output.c_str(), output.length());
                    }
                } else {
                    // No completions - ring the bell
                    write(STDOUT_FILENO, "\a", 1);
                }
            }
            // If we're typing arguments, tab does nothing
        } else if (ch >= 32 && ch < 127) {  // Printable characters
            line += ch;
            write(STDOUT_FILENO, &ch, 1);
        }
    }
    
    return line;
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;
    
    // Set terminal to raw mode for character-by-character input
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    while (true) {
        cout << "$ ";
        string input = readLineWithCompletion();
        
        // Restore terminal for command execution
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

        // Check for output redirection (>, 1>, 2>, >>, 1>>, 2>>)
        string redirectStdoutFile;
        string redirectStderrFile;
        bool hasStdoutRedirect = false;
        bool hasStderrRedirect = false;
        bool appendStdout = false;
        bool appendStderr = false;
        vector<string> cmdArgs;
        
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") {
                if (i + 1 < args.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = false;
                    redirectStdoutFile = args[i + 1];
                    i++; // Skip the filename
                }
            } else if (args[i] == ">>") {
                if (i + 1 < args.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = true;
                    redirectStdoutFile = args[i + 1];
                    i++; // Skip the filename
                }
            } else if (args[i] == "1>>") {
                if (i + 1 < args.size()) {
                    hasStdoutRedirect = true;
                    appendStdout = true;
                    redirectStdoutFile = args[i + 1];
                    i++; // Skip the filename
                }
            } else if (args[i] == "2>") {
                if (i + 1 < args.size()) {
                    hasStderrRedirect = true;
                    appendStderr = false;
                    redirectStderrFile = args[i + 1];
                    i++; // Skip the filename
                }
            } else if (args[i] == "2>>") {
                if (i + 1 < args.size()) {
                    hasStderrRedirect = true;
                    appendStderr = true;
                    redirectStderrFile = args[i + 1];
                    i++; // Skip the filename
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

        // Set up output redirection if needed
        int saved_stdout = -1;
        int saved_stderr = -1;
        
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
                if (hasStdoutRedirect && saved_stdout >= 0) {
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                }
                tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
                continue;
            }
            dup2(fd, STDERR_FILENO);
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
        
        // Restore stdout and stderr if they were redirected
        if (hasStdoutRedirect && saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        if (hasStderrRedirect && saved_stderr >= 0) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
        }
        
        // Set terminal back to raw mode for next command
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    }
    
    // Restore terminal on exit
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    return 0;
}