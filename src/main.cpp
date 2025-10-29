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

// ===== Configuration =====
class ShellConfig {
public:
    static vector<string> getBuiltinCommands() {
        return {"echo", "exit", "type", "pwd", "cd", "history"};
    }
    
    static bool isBuiltin(const string& cmd) {
        auto builtins = getBuiltinCommands();
        return find(builtins.begin(), builtins.end(), cmd) != builtins.end();
    }
};

// ===== History Management =====
class HistoryManager {
private:
    vector<string> commands;
    size_t lastWrittenIndex = 0;
    string historyFilePath;

public:
    void loadFromFile() {
        const char* histfile = getenv("HISTFILE");
        if (!histfile) return;
        
        historyFilePath = histfile;
        ifstream file(historyFilePath);
        if (!file.is_open()) return;
        
        string line;
        while (getline(file, line)) {
            if (!line.empty()) {
                commands.push_back(line);
            }
        }
        lastWrittenIndex = commands.size();
    }

    void saveToFile() {
        if (historyFilePath.empty()) return;
        
        ofstream file(historyFilePath);
        if (file.is_open()) {
            for (const auto& cmd : commands) {
                file << cmd << "\n";
            }
        }
    }

    void appendToFile(const string& filename) {
        ofstream file(filename, ios::app);
        if (file.is_open()) {
            for (size_t i = lastWrittenIndex; i < commands.size(); ++i) {
                file << commands[i] << "\n";
            }
            lastWrittenIndex = commands.size();
        }
    }

    void writeToFile(const string& filename) {
        ofstream file(filename);
        if (file.is_open()) {
            for (const auto& cmd : commands) {
                file << cmd << "\n";
            }
            lastWrittenIndex = commands.size();
        }
    }

    void readFromFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) return;
        
        string line;
        while (getline(file, line)) {
            if (!line.empty()) {
                commands.push_back(line);
            }
        }
        lastWrittenIndex = commands.size();
    }

    void add(const string& command) { 
        if (!command.empty()) commands.push_back(command); 
    }
    
    const vector<string>& getAll() const { return commands; }
    size_t size() const { return commands.size(); }
    string get(size_t index) const { return commands[index]; }
};

// ===== Utility Functions =====
class ShellUtils {
public:
    static string getCurrentDirectory() {
        char buffer[PATH_MAX];
        return getcwd(buffer, sizeof(buffer)) ? string(buffer) : "";
    }

    static string findInPath(const string& program) {
        const char* path = getenv("PATH");
        if (!path) return "";
        
        stringstream ss(path);
        string dir;
        while (getline(ss, dir, ':')) {
            string fullPath = dir + "/" + program;
            if (access(fullPath.c_str(), X_OK) == 0) {
                return fullPath;
            }
        }
        return "";
    }

    static vector<string> getExecutablesInPath(const string& prefix) {
        vector<string> executables;
        const char* path = getenv("PATH");
        if (!path) return executables;
        
        stringstream ss(path);
        string dir;
        while (getline(ss, dir, ':')) {
            if (dir.empty()) continue;
            
            DIR* dirp = opendir(dir.c_str());
            if (!dirp) continue;
            
            struct dirent* entry;
            while ((entry = readdir(dirp)) != nullptr) {
                string filename = entry->d_name;
                if (filename == "." || filename == "..") continue;
                if (filename.find(prefix) != 0) continue;
                
                string fullPath = dir + "/" + filename;
                if (access(fullPath.c_str(), X_OK) == 0) {
                    if (find(executables.begin(), executables.end(), filename) == executables.end()) {
                        executables.push_back(filename);
                    }
                }
            }
            closedir(dirp);
        }
        return executables;
    }

    static vector<string> parseInput(const string &input) {
        vector<string> args;
        string current;
        bool inQuotes = false;
        char quoteChar = '\0';

        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            
            // Handle backslash escaping
            if (c == '\\' && i + 1 < input.size()) {
                if (!inQuotes) {
                    current += input[++i];
                    continue;
                } else if (inQuotes && quoteChar == '"') {
                    char next = input[i + 1];
                    if (next == '"' || next == '\\' || next == '$') {
                        current += next;
                        ++i;
                        continue;
                    }
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
};

// ===== Tab Completion =====
class TabCompleter {
public:
    static vector<string> findCompletions(const string& prefix) {
        vector<string> completions;
        if (prefix.empty()) return completions;
        
        // Add matching builtins
        for (const auto& builtin : ShellConfig::getBuiltinCommands()) {
            if (builtin.find(prefix) == 0) {
                completions.push_back(builtin);
            }
        }
        
        // Add matching executables
        for (const auto& exe : ShellUtils::getExecutablesInPath(prefix)) {
            if (find(completions.begin(), completions.end(), exe) == completions.end()) {
                completions.push_back(exe);
            }
        }
        
        // Sort completions alphabetically
        sort(completions.begin(), completions.end());
        
        return completions;
    }

    static string findCommonPrefix(const vector<string>& strings) {
        if (strings.empty()) return "";
        string prefix = strings[0];
        
        for (size_t i = 1; i < strings.size() && !prefix.empty(); ++i) {
            size_t j = 0;
            while (j < prefix.length() && j < strings[i].length() && prefix[j] == strings[i][j]) {
                ++j;
            }
            prefix = prefix.substr(0, j);
        }
        return prefix;
    }
};

// ===== Input Handler =====
class InputHandler {
private:
    HistoryManager& history;
    string currentLine;
    int historyIndex;
    int tabPressCount;

    void handleArrowKey(char arrowType) {
        if (history.getAll().empty()) return;
        
        if (historyIndex == (int)history.size()) {
            currentLine = line;
        }
        
        if (arrowType == 'A' && historyIndex > 0) historyIndex--;
        if (arrowType == 'B' && historyIndex < (int)history.size() - 1) historyIndex++;
        
        if (historyIndex >= 0 && historyIndex < (int)history.size()) {
            line = history.get(historyIndex);
        } else if (arrowType == 'B' && historyIndex == (int)history.size()) {
            line = currentLine;
        }
        
        updateDisplay();
    }

    void handleTabCompletion() {
        size_t lastSpace = line.find_last_of(' ');
        string currentWord = (lastSpace == string::npos) ? line : line.substr(lastSpace + 1);

        if (lastSpace != string::npos || currentWord.empty()) return;

        auto completions = TabCompleter::findCompletions(currentWord);
        
        if (completions.empty()) {
            write(STDOUT_FILENO, "\a", 1);
        } else if (completions.size() == 1) {
            completeWord(completions[0], currentWord);
        } else {
            handleMultipleCompletions(completions, currentWord);
        }
    }

    void completeWord(const string& completion, const string& currentWord) {
        string toAdd = completion.substr(currentWord.size()) + " ";
        line += toAdd;
        write(STDOUT_FILENO, toAdd.c_str(), toAdd.size());
        tabPressCount = 0;
    }

    void handleMultipleCompletions(const vector<string>& completions, const string& currentWord) {
        string lcp = TabCompleter::findCommonPrefix(completions);
        if (lcp.size() > currentWord.size()) {
            string toAdd = lcp.substr(currentWord.size());
            line += toAdd;
            write(STDOUT_FILENO, toAdd.c_str(), toAdd.size());
        } else {
            write(STDOUT_FILENO, "\a", 1);
        }

        if (++tabPressCount == 2) {
            showCompletionsList(completions);
            tabPressCount = 0;
        }
    }

    void showCompletionsList(const vector<string>& completions) {
        string output = "\n";
        for (const auto& comp : completions) {
            output += comp + "  ";
        }
        output += "\n$ " + line;
        write(STDOUT_FILENO, output.c_str(), output.size());
    }

    void updateDisplay() {
        write(STDOUT_FILENO, "\r$ \033[K", 6);
        write(STDOUT_FILENO, line.c_str(), line.length());
    }

    void resetHistoryState() {
        historyIndex = history.size();
        currentLine.clear();
        tabPressCount = 0;
    }

public:
    string line;

    InputHandler(HistoryManager& hist) : history(hist), historyIndex(hist.size()), tabPressCount(0) {}

    string readLine() {
        line.clear();
        char ch;
        
        while (read(STDIN_FILENO, &ch, 1) == 1) {
            if (ch == '\x1b') {
                handleEscapeSequence();
            } else if (ch == '\n') {
                write(STDOUT_FILENO, "\n", 1);
                break;
            } else if (ch == 127 || ch == 8) {
                handleBackspace();
            } else if (ch == '\t') {
                handleTabCompletion();
            } else if (ch >= 32 && ch < 127) {
                handlePrintableChar(ch);
            }
        }
        return line;
    }

private:
    void handleEscapeSequence() {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return;
        
        if (seq[0] == '[' && (seq[1] == 'A' || seq[1] == 'B')) {
            handleArrowKey(seq[1]);
        }
    }

    void handleBackspace() {
        if (!line.empty()) {
            line.pop_back();
            write(STDOUT_FILENO, "\b \b", 3);
        }
        resetHistoryState();
    }

    void handlePrintableChar(char ch) {
        line += ch;
        write(STDOUT_FILENO, &ch, 1);
        resetHistoryState();
    }
};

// ===== Command Execution =====
class CommandExecutor {
private:
    HistoryManager& history;

    void executeExternalCommand(const vector<string>& cmdArgs) {
        string path = ShellUtils::findInPath(cmdArgs[0]);
        if (path.empty()) {
            cout << cmdArgs[0] << ": command not found\n";
            return;
        }

        pid_t pid = fork();
        if (pid == 0) {
            vector<char*> execArgs;
            for (const auto& arg : cmdArgs) {
                execArgs.push_back(const_cast<char*>(arg.c_str()));
            }
            execArgs.push_back(nullptr);
            execv(path.c_str(), execArgs.data());
            perror("execv failed");
            exit(1);
        } else if (pid > 0) {
            waitpid(pid, nullptr, 0);
        }
    }

    bool setupRedirection(int& saved_fd, int fd, const string& filename, int flags) {
        saved_fd = dup(fd);
        int new_fd = open(filename.c_str(), flags, 0644);
        if (new_fd < 0) {
            cerr << "Error opening file: " << filename << "\n";
            return false;
        }
        dup2(new_fd, fd);
        close(new_fd);
        return true;
    }

    void restoreRedirection(int saved_fd, int fd) {
        if (saved_fd >= 0) {
            dup2(saved_fd, fd);
            close(saved_fd);
        }
    }

public:
    CommandExecutor(HistoryManager& hist) : history(hist) {}

    void executeBuiltin(const vector<string>& cmdArgs, int in_fd = -1, int out_fd = -1) {
        if (cmdArgs.empty()) return;
        
        string cmd = cmdArgs[0];
        int saved_stdin = -1, saved_stdout = -1;

        // Setup redirection
        if (in_fd != -1) { saved_stdin = dup(STDIN_FILENO); dup2(in_fd, STDIN_FILENO); close(in_fd); }
        if (out_fd != -1) { saved_stdout = dup(STDOUT_FILENO); dup2(out_fd, STDOUT_FILENO); close(out_fd); }

        // Execute command
        if (cmd == "echo") {
            for (size_t i = 1; i < cmdArgs.size(); ++i) {
                cout << cmdArgs[i] << (i != cmdArgs.size()-1 ? " " : "");
            }
            cout << "\n";
        } else if (cmd == "pwd") {
            cout << ShellUtils::getCurrentDirectory() << "\n";
        } else if (cmd == "cd") {
            string path = cmdArgs.size() < 2 ? getenv("HOME") ?: "" : cmdArgs[1];
            if (path == "~") path = getenv("HOME") ?: "~";
            if (chdir(path.c_str()) != 0) {
                cerr << "cd: " << path << ": No such file or directory\n";
            }
        } else if (cmd == "type") {
            if (cmdArgs.size() >= 2) {
                string name = cmdArgs[1];
                if (ShellConfig::isBuiltin(name)) {
                    cout << name << " is a shell builtin\n";
                } else {
                    string path = ShellUtils::findInPath(name);
                    cout << name << (path.empty() ? ": not found" : " is " + path) << "\n";
                }
            }
        } else if (cmd == "history") {
            handleHistoryCommand(cmdArgs);
        }

        // Restore redirection
        restoreRedirection(saved_stdin, STDIN_FILENO);
        restoreRedirection(saved_stdout, STDOUT_FILENO);
    }

    void execute(const vector<string>& cmdArgs, 
                 const string& stdoutFile = "", bool appendStdout = false,
                 const string& stderrFile = "", bool appendStderr = false) {
        if (cmdArgs.empty()) return;
        
        int saved_stdout = -1, saved_stderr = -1;
        bool stdoutSuccess = true, stderrSuccess = true;

        // Setup stdout redirection
        if (!stdoutFile.empty()) {
            int flags = O_WRONLY | O_CREAT | (appendStdout ? O_APPEND : O_TRUNC);
            stdoutSuccess = setupRedirection(saved_stdout, STDOUT_FILENO, stdoutFile, flags);
        }

        // Setup stderr redirection  
        if (!stderrFile.empty()) {
            int flags = O_WRONLY | O_CREAT | (appendStderr ? O_APPEND : O_TRUNC);
            stderrSuccess = setupRedirection(saved_stderr, STDERR_FILENO, stderrFile, flags);
        }

        // Execute command only if redirections were successful
        if ((stdoutFile.empty() || stdoutSuccess) && (stderrFile.empty() || stderrSuccess)) {
            if (ShellConfig::isBuiltin(cmdArgs[0])) {
                executeBuiltin(cmdArgs);
            } else {
                executeExternalCommand(cmdArgs);
            }
        }

        // Restore redirection
        restoreRedirection(saved_stdout, STDOUT_FILENO);
        restoreRedirection(saved_stderr, STDERR_FILENO);
    }

private:
    void handleHistoryCommand(const vector<string>& cmdArgs) {
        if (cmdArgs.size() >= 3) {
            string flag = cmdArgs[1], filename = cmdArgs[2];
            if (flag == "-r") history.readFromFile(filename);
            else if (flag == "-w") history.writeToFile(filename);
            else if (flag == "-a") history.appendToFile(filename);
            return;
        }
        
        // Display history
        size_t start_index = 0;
        size_t count = history.size();
        
        if (cmdArgs.size() >= 2) {
            try {
                int n = stoi(cmdArgs[1]);
                if (n > 0 && (size_t)n < count) start_index = count - n;
            } catch (...) {}
        }
        
        for (size_t i = start_index; i < count; ++i) {
            cout << "    " << (i + 1) << "  " << history.get(i) << "\n";
        }
    }
};

// ===== Main Shell Class =====
class Shell {
private:
    HistoryManager history;
    CommandExecutor executor;
    struct termios old_tio, new_tio;

    void setupTerminal() {
        tcgetattr(STDIN_FILENO, &old_tio);
        new_tio = old_tio;
        new_tio.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    }

    void restoreTerminal() {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    }

    vector<vector<string>> parsePipeline(const vector<string>& args) {
        vector<vector<string>> commands;
        vector<string> currentCmd;
        
        for (const auto& arg : args) {
            if (arg == "|") {
                if (!currentCmd.empty()) commands.push_back(currentCmd);
                currentCmd.clear();
            } else {
                currentCmd.push_back(arg);
            }
        }
        if (!currentCmd.empty()) commands.push_back(currentCmd);
        return commands;
    }

    struct RedirectionInfo {
        string stdoutFile;
        string stderrFile;
        bool appendStdout = false;
        bool appendStderr = false;
        vector<string> filteredArgs;
    };

    RedirectionInfo parseRedirections(const vector<string>& args) {
        RedirectionInfo info;
        info.filteredArgs = args;

        for (size_t i = 0; i < info.filteredArgs.size(); ) {
            const string& arg = info.filteredArgs[i];
            
            if (arg == ">" || arg == "1>") {
                if (i + 1 < info.filteredArgs.size()) {
                    info.stdoutFile = info.filteredArgs[i + 1];
                    info.appendStdout = false;
                    info.filteredArgs.erase(info.filteredArgs.begin() + i, info.filteredArgs.begin() + i + 2);
                } else {
                    i++;
                }
            } else if (arg == ">>" || arg == "1>>") {
                if (i + 1 < info.filteredArgs.size()) {
                    info.stdoutFile = info.filteredArgs[i + 1];
                    info.appendStdout = true;
                    info.filteredArgs.erase(info.filteredArgs.begin() + i, info.filteredArgs.begin() + i + 2);
                } else {
                    i++;
                }
            } else if (arg == "2>") {
                if (i + 1 < info.filteredArgs.size()) {
                    info.stderrFile = info.filteredArgs[i + 1];
                    info.appendStderr = false;
                    info.filteredArgs.erase(info.filteredArgs.begin() + i, info.filteredArgs.begin() + i + 2);
                } else {
                    i++;
                }
            } else if (arg == "2>>") {
                if (i + 1 < info.filteredArgs.size()) {
                    info.stderrFile = info.filteredArgs[i + 1];
                    info.appendStderr = true;
                    info.filteredArgs.erase(info.filteredArgs.begin() + i, info.filteredArgs.begin() + i + 2);
                } else {
                    i++;
                }
            } else {
                i++;
            }
        }

        return info;
    }

    void executePipeline(const vector<vector<string>>& commands) {
        int numCommands = commands.size();
        vector<pid_t> pids;
        vector<vector<int>> pipes(numCommands - 1, vector<int>(2));

        // Create pipes
        for (int i = 0; i < numCommands - 1; i++) {
            if (pipe(pipes[i].data()) == -1) {
                perror("pipe");
                return;
            }
        }

        // Execute commands
        for (int i = 0; i < numCommands; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
                if (i < numCommands - 1) dup2(pipes[i][1], STDOUT_FILENO);
                
                for (int j = 0; j < numCommands - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                
                if (ShellConfig::isBuiltin(commands[i][0])) {
                    executor.executeBuiltin(commands[i]);
                    exit(0);
                } else {
                    vector<char*> execArgs;
                    for (const auto& arg : commands[i]) {
                        execArgs.push_back(const_cast<char*>(arg.c_str()));
                    }
                    execArgs.push_back(nullptr);
                    
                    string path = ShellUtils::findInPath(commands[i][0]);
                    if (path.empty()) {
                        cerr << commands[i][0] << ": command not found\n";
                        exit(1);
                    }
                    execv(path.c_str(), execArgs.data());
                    perror("execv failed");
                    exit(1);
                }
            } else if (pid > 0) {
                pids.push_back(pid);
            }
        }

        // Cleanup
        for (int i = 0; i < numCommands - 1; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        for (pid_t pid : pids) waitpid(pid, nullptr, 0);
    }

public:
    Shell() : executor(history) {
        cout << unitbuf;
        cerr << unitbuf;
        history.loadFromFile();
        setupTerminal();
    }

    ~Shell() {
        history.saveToFile();
        restoreTerminal();
    }

    void run() {
        while (true) {
            cout << "$ ";
            
            InputHandler input(history);
            string line = input.readLine();
            restoreTerminal();

            if (line.empty()) continue;

            history.add(line);
            vector<string> args = ShellUtils::parseInput(line);

            if (args.empty()) continue;

            // Handle exit command
            if (args[0] == "exit") {
                int exitCode = 0;
                if (args.size() >= 2) {
                    try { exitCode = stoi(args[1]); } catch (...) {}
                }
                return;
            }

            // Handle pipelines
            auto commands = parsePipeline(args);
            if (commands.size() > 1) {
                executePipeline(commands);
            } else {
                // Parse and handle redirections for single command
                RedirectionInfo redirInfo = parseRedirections(args);
                if (redirInfo.filteredArgs.empty()) continue;
                
                executor.execute(redirInfo.filteredArgs,
                                redirInfo.stdoutFile, redirInfo.appendStdout,
                                redirInfo.stderrFile, redirInfo.appendStderr);
            }

            setupTerminal();
        }
    }
};

// ===== Main Function =====
int main() {
    Shell shell;
    shell.run();
    return 0;
}