#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h> // for access()
#include <cstdlib>  // for getenv()
using namespace std;

bool is_builtin(const string &cmd) {
    return (cmd == "echo" || cmd == "exit" || cmd == "type");
}

string find_in_path(const string &cmd) {
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

        vector<string> parts;
        string word;
        stringstream ss(input);
        while (ss >> word) parts.push_back(word);
        if (parts.empty()) continue;

        if (input == "exit 0") return 0;

        else if (parts[0] == "echo") {
            for (size_t i = 1; i < parts.size(); ++i)
                cout << parts[i] << (i + 1 < parts.size() ? " " : "\n");
        }

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

        else
            cout << input << ": command not found\n";
    }

    return 0;
}
