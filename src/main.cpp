#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <map>
#include <queue>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <limits>
#include "terminalpassthrough.h"
#include "nlohmann/json.hpp"
#include "openaipromptengine.h"

using json = nlohmann::json;

bool TESTING = false;
bool shotcutsEnabled = true;
bool startCommandsOn = true;
bool runningStartup = false;
bool exitFlag = false;
bool defaultTextEntryOnAI = false;
bool usingChatCache = true;
bool saveLoop = false;
bool saveOnExit = true;
bool rawEnabled = false;
bool displayWholePath = false;
bool checkForUpdates = true;

const std::string GREEN_COLOR_BOLD = "\033[1;32m";
const std::string RESET_COLOR = "\033[0m";
const std::string RED_COLOR_BOLD = "\033[1;31m";
const std::string PURPLE_COLOR_BOLD = "\033[1;35m";
const std::string updateURL = "https://api.github.com/repos/cadenfinley/DevToolsTerminal/releases/latest";
const std::string githubRepoURL = "https://github.com/CadenFinley/DevToolsTerminal";
const std::string currentVersion = "1.3.1";

std::string commandPrefix = "!";
std::string lastCommandParsed;
std::string applicationDirectory;
std::string titleLine = "DevToolsTerminal v" + currentVersion + " - Caden Finley (c) 2025";
std::string createdLine = "Created 2025 @ " + PURPLE_COLOR_BOLD + "Abilene Christian University" + RESET_COLOR;

std::filesystem::path DATA_DIRECTORY = ".DTT-Data";
std::filesystem::path USER_DATA = DATA_DIRECTORY / ".USER_DATA.json";
std::filesystem::path USER_COMMAND_HISTORY = DATA_DIRECTORY / ".USER_COMMAND_HISTORY.txt";

std::queue<std::string> commandsQueue;
std::vector<std::string> startupCommands;
std::vector<std::string> savedChatCache;
std::vector<std::string> commandLines;
std::map<std::string, std::string> shortcuts;
std::map<std::string, std::vector<std::string>> multiScriptShortcuts;

OpenAIPromptEngine openAIPromptEngine;
TerminalPassthrough terminal;

std::string readAndReturnUserDataFile();
std::vector<std::string> commandSplicer(const std::string& command);
void mainProcessLoop();
void createNewUSER_DATAFile();
void createNewUSER_HISTORYfile();
void loadUserData();
void writeUserData();
void goToApplicationDirectory();
void commandParser(const std::string& command);
void addUserInputToHistory(const std::string& input);
void shortcutProcesser(const std::string& command);
void commandProcesser(const std::string& command);
void sendTerminalCommand(const std::string& command);
void userSettingsCommands();
void startupCommandsHandler();
void shortcutCommands();
void textCommands();
void getNextCommand();
void aiSettingsCommands();
void aiChatCommands();
void chatProcess(const std::string& message);
void showChatHistory();
void multiScriptShortcutCommands();
void userDataCommands();
void setRawMode(bool enable);
void handleArrowKey(char arrow, size_t& cursorPositionX, size_t& cursorPositionY, std::vector<std::string>& commandLines, std::string& command, const std::string& terminalTag);
void placeCursor(size_t& cursorPositionX, size_t& cursorPositionY);
void reprintCommandLines(const std::vector<std::string>& commandLines, const std::string& terminalSetting);
void clearLines(const std::vector<std::string>& commandLines);
bool checkForUpdate();
bool downloadLatestRelease();
void displayChangeLog(const std::string& changeLog);

int main() {
    sendTerminalCommand("clear");
    std::cout << "Loading..." << std::endl;

    if(checkForUpdates){
        if (checkForUpdate()) {
            std::cout << "An update is available. Would you like to download it? (Y/N)" << std::endl;
            char response;
            std::cin >> response;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (response == 'Y' || response == 'y') {
                if (!downloadLatestRelease()) {
                    std::cout << "Failed to download the update. Please try again later." << std::endl;
                }
            }
        } else {
            std::cout << "You are up to date!." << std::endl;
        }
    }

    std::ifstream changelogFile(DATA_DIRECTORY / "CHANGELOG.txt");
    if (changelogFile.is_open()) {
        std::cout << "Thanks for downloading the latest version of DevToolsTerminal Version: " << currentVersion << std::endl;
        std::cout << "Check out the github repo for more information:\n" << githubRepoURL << std::endl;
        std::cout << "And check me out at CadenFinley.com" << std::endl;
        std::string changeLog((std::istreambuf_iterator<char>(changelogFile)), std::istreambuf_iterator<char>());
        changelogFile.close();
        displayChangeLog(changeLog);
        std::cout << "Press enter to continue..." << std::endl;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::filesystem::remove(DATA_DIRECTORY / "CHANGELOG.txt");
    }

    startupCommands = {};
    shortcuts = {};
    multiScriptShortcuts = {};
    terminal = TerminalPassthrough();
    openAIPromptEngine = OpenAIPromptEngine("", "chat", "You are an AI personal assistant within a terminal application.", {}, ".DTT-Data");

    applicationDirectory = std::filesystem::current_path().string();
    if (applicationDirectory.find(":") != std::string::npos) {
        applicationDirectory = applicationDirectory.substr(applicationDirectory.find(":") + 1);
    }

    if (!std::filesystem::exists(DATA_DIRECTORY)) {
        std::cout << DATA_DIRECTORY.string() << " not found in: " << applicationDirectory << std::endl;
        std::filesystem::create_directory(applicationDirectory / DATA_DIRECTORY);
    }

    if (!std::filesystem::exists(USER_DATA)) {
        createNewUSER_DATAFile();
    } else {
        loadUserData();
    }

    if (!std::filesystem::exists(USER_COMMAND_HISTORY)) {
        createNewUSER_HISTORYfile();
    }

    if (!startupCommands.empty() && startCommandsOn) {
        runningStartup = true;
        std::cout << "Running startup commands..." << std::endl;
        for (const auto& command : startupCommands) {
            commandParser(commandPrefix + command);
        }
        runningStartup = false;
    }

    std::cout << titleLine << std::endl;
    std::cout << createdLine << std::endl;

    mainProcessLoop();
    std::cout << "Exiting..." << std::endl;
    if(saveOnExit){
        savedChatCache = openAIPromptEngine.getChatCache();
        writeUserData();
    }
    setRawMode(false);
    return 0;
}

int getTerminalWidth(){
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

void mainProcessLoop() {
    std::string terminalSetting;
    int terminalSettingLength;
    setRawMode(true);
    while (true) {
        if (saveLoop) {
            writeUserData();
        }
        if (TESTING) {
            std::cout << RED_COLOR_BOLD << "DEV MODE ENABLED" << RESET_COLOR << std::endl;
        }
        if (defaultTextEntryOnAI) {
            terminalSetting = GREEN_COLOR_BOLD + "AI Menu: " + RESET_COLOR;
            terminalSettingLength = 9;
        } else {
            terminalSetting = terminal.returnCurrentTerminalPosition();
            terminalSettingLength = terminal.getTerminalCurrentPositionRawLength();
        }
        std::cout << terminalSetting;
        char c;
        size_t cursorPositionX = 0;
        size_t cursorPositionY = 0;
        commandLines.clear();
        commandLines.push_back("");
        while (true) {
            std::cin.get(c);
            if (c == '\033') {
                std::cin.get(c);
                if (c == '[') {
                    std::cin.get(c);
                    handleArrowKey(c, cursorPositionX, cursorPositionY, commandLines, commandLines[cursorPositionY], terminalSetting);
                }
            } else if (c == '\n') {
                std::cout << std::endl;
                break;
            } else if (c == 127) {
                clearLines(commandLines);
                if (commandLines[cursorPositionY].length() > 0 && cursorPositionX > 0) {
                    commandLines[cursorPositionY].erase(cursorPositionX - 1, 1);
                    cursorPositionX--;
                } else if (cursorPositionX == 0 && cursorPositionY > 0) {
                    cursorPositionX = commandLines[cursorPositionY - 1].length();
                    commandLines[cursorPositionY - 1] += commandLines[cursorPositionY];
                    commandLines.erase(commandLines.begin() + cursorPositionY);
                    cursorPositionY--;
                }
                reprintCommandLines(commandLines, terminalSetting);
                placeCursor(cursorPositionX, cursorPositionY);
            } else {
                clearLines(commandLines);
                commandLines[cursorPositionY].insert(cursorPositionX, 1, c);
                int currentLineLength;
                if(cursorPositionY == 0){
                    currentLineLength = commandLines[cursorPositionY].length() + terminalSettingLength;
                } else {
                    currentLineLength = commandLines[cursorPositionY].length();
                }
                if (currentLineLength < getTerminalWidth()) {
                    cursorPositionX++;
                } else {
                    cursorPositionY++;
                    commandLines.push_back("");
                    cursorPositionX = 0;
                }
                reprintCommandLines(commandLines, terminalSetting);
                placeCursor(cursorPositionX, cursorPositionY);
            }
        }
        std::string finalCommand;
        for (const auto& line : commandLines) {
            finalCommand += line;
        }
        setRawMode(false);
        commandParser(finalCommand);
        setRawMode(true);
        if (exitFlag) {
            break;
        }
    }
}

void clearLines(const std::vector<std::string>& commandLines){
    std::cout << "\033[2K\r";
    if(commandLines.size() > 1){
        for (int i = 0; i < commandLines.size(); i++) {
            if (i > 0) {
                std::cout << "\033[A";
            }
            std::cout << "\033[2K\r";
        }
    }
}

void reprintCommandLines(const std::vector<std::string>& commandLines, const std::string& terminalSetting) {
    for (int i = 0; i < commandLines.size(); i++) {
        if (i == 0) {
            std::cout << terminalSetting << commandLines[i];
        } else {
            std::cout << commandLines[i];
        }
        if (i < commandLines.size() - 1) {
            std::cout << std::endl;
        }
    }
}

void placeCursor(size_t& cursorPositionX, size_t& cursorPositionY){
    int columnsBehind = commandLines[cursorPositionY].length() - cursorPositionX;
    int rowsBehind = commandLines.size() - cursorPositionY - 1;
    if (columnsBehind > 0) {
        while (columnsBehind > 0) {
            std::cout << "\033[D";
            columnsBehind--;
        }
    }
    if(rowsBehind > 0){
        while (rowsBehind > 0) {
            std::cout << "\033[A";
            rowsBehind--;
        }
    }
}

void setRawMode(bool enable) {
    static struct termios oldt, newt;
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
    rawEnabled = enable;
}

void handleArrowKey(char arrow, size_t& cursorPositionX, size_t& cursorPositionY, std::vector<std::string>& commandLines, std::string& command, const std::string& terminalTag) {
    switch (arrow) {
        case 'A':
            clearLines(commandLines);
            commandLines.clear();
            commandLines.push_back("");
            command = terminal.getPreviousCommand();
            cursorPositionX = command.length();
            cursorPositionY = commandLines.size() - 1;
            std::cout << "\033[2K\r" << terminalTag << command;
            break;
        case 'B':
            clearLines(commandLines);
            commandLines.clear();
            commandLines.push_back("");
            command = terminal.getNextCommand();
            cursorPositionX = command.length();
            cursorPositionY = commandLines.size() - 1;
            std::cout << "\033[2K\r" << terminalTag << command;
            break;
        case 'C':
            if (cursorPositionX < command.length()) {
                cursorPositionX++;
                std::cout << "\033[C";
            } else if (cursorPositionY < commandLines.size() - 1) {
                cursorPositionY++;
                cursorPositionX = 0;
                std::cout << "\033[B";
            }
            break;
        case 'D':
            if (cursorPositionX > 0) {
                cursorPositionX--;
                std::cout << "\033[D";
            } else if (cursorPositionY > 0) {
                cursorPositionY--;
                cursorPositionX = commandLines[cursorPositionY].length();
                std::cout << "\033[A";
            }
            break;
    }
}

void createNewUSER_DATAFile() {
    std::cout << "User data file not found. Creating new file..." << std::endl;
    std::ofstream file(USER_DATA);
    if (file.is_open()) {
        startupCommands.push_back("terminal cd /");
        writeUserData();
        file.close();
    } else {
        std::cerr << "Error: Unable to create the user data file at " << USER_DATA << std::endl;
    }
}

void createNewUSER_HISTORYfile() {
    std::cout << "User history file not found. Creating new file..." << std::endl;
    std::ofstream file(USER_COMMAND_HISTORY);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to create the user history file at " << USER_COMMAND_HISTORY << std::endl;
    }
}

void loadUserData() {
    std::ifstream file(USER_DATA);
    if (file.is_open()) {
        if (file.peek() == std::ifstream::traits_type::eof()) {
            std::cout << "User data file is empty. Creating new file..." << std::endl;
            file.close();
            createNewUSER_DATAFile();
            return;
        }
        try {
            json userData;
            file >> userData;
            if(userData.contains("OpenAI_API_KEY")){
                openAIPromptEngine.setAPIKey(userData["OpenAI_API_KEY"].get<std::string>());
            }
            if(userData.contains("Chat_Cache")) {
                savedChatCache = userData["Chat_Cache"].get<std::vector<std::string>>();
                openAIPromptEngine.setChatCache(savedChatCache);
            }
            if(userData.contains("Startup_Commands")){
                startupCommands = userData["Startup_Commands"].get<std::vector<std::string>>();
            }
            if(userData.contains("Shortcuts_Enabled")){
                shotcutsEnabled = userData["Shortcuts_Enabled"].get<bool>();
            }
            if(userData.contains("Shortcuts")){
                shortcuts = userData["Shortcuts"].get<std::map<std::string, std::string>>();
            }
            if(userData.contains("Text_Entry")){
                defaultTextEntryOnAI = userData["Text_Entry"].get<bool>();
            }
            if(userData.contains("Command_Prefix")){
                commandPrefix = userData["Command_Prefix"].get<std::string>();
            }
            if(userData.contains("Multi_Script_Shortcuts")){
                multiScriptShortcuts = userData["Multi_Script_Shortcuts"].get<std::map<std::string, std::vector<std::string>>>();
            }
            file.close();
        }
        catch(const json::parse_error& e) {
            std::cerr << "Error parsing user data file: " << e.what() << "\nCreating new file." << std::endl;
            file.close();
            createNewUSER_DATAFile();
            return;
        }
    } else {
        std::cerr << "Error: Unable to read the user data file at " << USER_DATA << std::endl;
    }
}

void writeUserData() {
    std::ofstream file(USER_DATA);
    if (file.is_open()) {
        json userData;
        userData["OpenAI_API_KEY"] = openAIPromptEngine.getAPIKey();
        userData["Chat_Cache"] = savedChatCache;
        userData["Startup_Commands"] = startupCommands;
        userData["Shortcuts_Enabled"] = shotcutsEnabled;
        userData["Shortcuts"] = shortcuts;
        userData["Text_Buffer"] = false;
        userData["Text_Entry"] = defaultTextEntryOnAI;
        userData["Command_Prefix"] = commandPrefix;
        userData["Multi_Script_Shortcuts"] = multiScriptShortcuts;
        file << userData.dump(4);
        file.close();
    } else {
        std::cerr << "Error: Unable to write to the user data file at " << USER_DATA << std::endl;
    }
}

void goToApplicationDirectory() {
    commandProcesser("terminal cd /");
    commandProcesser("terminal cd " + applicationDirectory +"/"+ DATA_DIRECTORY.string());
}

std::string readAndReturnUserDataFile() {
    std::ifstream file(USER_DATA);
    if (file.is_open()) {
        std::string userData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        return userData.empty() ? "No data found." : userData;
    } else {
        std::cerr << "Error: Unable to read the user data file at " << USER_DATA << std::endl;
        return "";
    }
}

std::vector<std::string> commandSplicer(const std::string& command) {
    std::vector<std::string> commands;
    std::istringstream iss(command);
    std::string word;
    std::string combined;
    bool inQuotes = false;
    char quoteChar = '\0';

    while (iss >> word) {
        if (!inQuotes && (word.front() == '\'' || word.front() == '"' || word.front() == '(' || word.front() == '[')) {
            inQuotes = true;
            quoteChar = word.front();
            combined = word.substr(1);
        } else if (inQuotes && word.back() == quoteChar) {
            combined += " " + word.substr(0, word.size() - 1);
            commands.push_back(combined);
            inQuotes = false;
            quoteChar = '\0';
        } else if (inQuotes) {
            combined += " " + word;
        } else {
            commands.push_back(word);
        }
    }

    if (inQuotes) {
        commands.push_back(combined);
    }

    return commands;
}

void commandParser(const std::string& command) {
    if (command.empty()) {
        return;
    }
    if (!runningStartup) {
        addUserInputToHistory(command);
    }
    if (command.rfind(commandPrefix, 0) == 0) {
        terminal.addCommandToHistory(command);
        commandProcesser(command.substr(1));
        return;
    }
    if (defaultTextEntryOnAI) {
        terminal.addCommandToHistory(command);
        chatProcess(command);
    } else {
        sendTerminalCommand(command);
    }
}

void addUserInputToHistory(const std::string& input) {
    std::ofstream file(USER_COMMAND_HISTORY, std::ios_base::app);
    if (file.is_open()) {
        file << std::to_string(time(nullptr)) << " " << input << "\n";
        file.close();
    } else {
        std::cerr << "Error: Unable to write to the user input history file at " << USER_COMMAND_HISTORY << std::endl;
    }
}

inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

void shortcutProcesser(const std::string& command) {
    if (!shotcutsEnabled) {
        std::cout << "Shortcuts are disabled." << std::endl;
        return;
    }
    if (!shortcuts.empty()) {
        std::string strippedCommand = command.substr(2);
        trim(strippedCommand);
        if (strippedCommand.empty()) {
            std::cout << "No shortcut given." << std::endl;
            return;
        }
        if (shortcuts.find(strippedCommand) != shortcuts.end()) {
            commandProcesser(shortcuts[strippedCommand]);
        } else {
            std::cout << "No command for given shortcut: " << strippedCommand << std::endl;
        }
    } else {
        std::cout << "No shortcuts have been created." << std::endl;
    }
}

void multiScriptShortcutProcesser(const std::string& command){
    if (!shotcutsEnabled) {
        std::cout << "Shortcuts are disabled." << std::endl;
        return;
    }
    if (!multiScriptShortcuts.empty()) {
        std::string strippedCommand = command.substr(2);
        trim(strippedCommand);
        if (strippedCommand.empty()) {
            std::cout << "No shortcut given." << std::endl;
            return;
        }
        if (multiScriptShortcuts.find(strippedCommand) != multiScriptShortcuts.end()) {
            for(const auto& command : multiScriptShortcuts[strippedCommand]){
                commandProcesser(command);
            }
        } else {
            std::cout << "No command for given shortcut: " << strippedCommand << std::endl;
        }
    } else {
        std::cout << "No smulti-script shortcuts have been created." << std::endl;
    }
}

void commandProcesser(const std::string& command) {
    commandsQueue = std::queue<std::string>();
    auto commands = commandSplicer(command);
    for (const auto& cmd : commands) {
        commandsQueue.push(cmd);
    }
    if (TESTING) {
        std::cout << "Commands Queue: ";
        for (const auto& cmd : commands) {
            std::cout << cmd << " ";
        }
        std::cout << std::endl;
    }
    if (commandsQueue.empty()) {
        std::cout << "Unknown command. Please try again." << std::endl;
    }
    getNextCommand();
    if (lastCommandParsed == "approot") {
        goToApplicationDirectory();
    } else if (lastCommandParsed == "clear") {
        std::cout << "Clearing screen and terminal cache..." << std::endl;
        std::cout << "\033[2J\033[1;1H";
        terminal.clearTerminalCache();
    } else if(lastCommandParsed == "ss"){
        shortcutProcesser(command);
    } else if(lastCommandParsed == "mm"){
        multiScriptShortcutProcesser(command);
    } else if (lastCommandParsed == "ai") {
        aiSettingsCommands();
    } else if (lastCommandParsed == "user") {
        userSettingsCommands();
    } else if (lastCommandParsed == "aihelp"){
        if (!defaultTextEntryOnAI && !openAIPromptEngine.getAPIKey().empty() ){
            std::string message = ("I am encountering these errors in the " + terminal.getTerminalName() + " and would like some help solving these issues. User input " + terminal.returnMostRecentUserInput() + " Terminal output " + terminal.returnMostRecentTerminalOutput());
            if (TESTING) {
                std::cout << message << std::endl;
            }
            std::cout << openAIPromptEngine.forceDirectChatGPT(message, false) << std::endl;
            return;
        }
    } else if(lastCommandParsed == "version") {
        std::cout << "DevToolsTerminal v" + currentVersion << std::endl;
    } else if (lastCommandParsed == "terminal") {
        try {
            std::string terminalCommand = command.substr(9);
            sendTerminalCommand(terminalCommand);
        } catch (std::out_of_range& e) {
            defaultTextEntryOnAI = false;
            return;
        }
    } else if (lastCommandParsed == "exit") {
        exitFlag = true;
    } else if (lastCommandParsed == "help") {
        std::cout << "Commands:" << std::endl;
        std::cout << "Command Prefix: " + commandPrefix << std::endl;
        std::cout << "ai" << std::endl;
        std::cout << "approot" << std::endl;
        std::cout << "terminal o[ARGS]" << std::endl;
        std::cout << "user" << std::endl;
        std::cout << "exit" << std::endl;
        std::cout << "clear" << std::endl;
        std::cout << "help" << std::endl;
    } else {
        std::cout << "Unknown command. Please try again or try 'help'." << std::endl;
    }
}

void sendTerminalCommand(const std::string& command) {
    if (TESTING) {
        std::cout << "Sending Command: " << command << std::endl;
    }
    if(command == "exit"){
        exitFlag = true;
        return;
    }
    std::thread commandThread = terminal.executeCommand(command);
    commandThread.join();
}

void userSettingsCommands() {
    getNextCommand();
    if (lastCommandParsed.empty()) {
        std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
        return;
    }
    if (lastCommandParsed == "startup") {
        startupCommandsHandler();
        return;
    }
    if (lastCommandParsed == "text") {
        textCommands();
        return;
    }
    if (lastCommandParsed == "shortcut") {
        shortcutCommands();
        return;
    }
    if (lastCommandParsed == "testing") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        if (lastCommandParsed == "enable") {
            TESTING = true;
            std::cout << "Testing mode enabled." << std::endl;
            return;
        }
        if (lastCommandParsed == "disable") {
            TESTING = false;
            std::cout << "Testing mode disabled." << std::endl;
            return;
        }
        std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
        return;
    }
    if (lastCommandParsed == "data") {
        userDataCommands();
        return;
    }
    if(lastCommandParsed == "saveloop"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Save loop is currently " << (saveLoop ? "enabled." : "disabled.") << std::endl;
            return;
        }
        if (lastCommandParsed == "enable") {
            saveLoop = true;
            std::cout << "Save loop enabled." << std::endl;
            return;
        }
        if (lastCommandParsed == "disable") {
            saveLoop = false;
            std::cout << "Save loop disabled." << std::endl;
            return;
        }
    }
    if(lastCommandParsed == "saveonexit"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Save on exit is currently " << (saveOnExit ? "enabled." : "disabled.") << std::endl;
            return;
        }
        if (lastCommandParsed == "enable") {
            saveOnExit = true;
            std::cout << "Save on exit enabled." << std::endl;
            return;
        }
        if (lastCommandParsed == "disable") {
            saveOnExit = false;
            std::cout << "Save on exit disabled." << std::endl;
            return;
        }
    }
    if(lastCommandParsed == "checkforupdates"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Check for updates is currently " << (checkForUpdates ? "enabled." : "disabled.") << std::endl;
            return;
        }
        if (lastCommandParsed == "enable") {
            checkForUpdates = true;
            std::cout << "Check for updates enabled." << std::endl;
            return;
        }
        if (lastCommandParsed == "disable") {
            checkForUpdates = false;
            std::cout << "Check for updates disabled." << std::endl;
            return;
        }
    }
    if (lastCommandParsed == "help") {
        std::cout << "Commands: " << std::endl;
        std::cout << "startup: add [ARGS], remove [ARGS], clear, enable, disable, list, runall" << std::endl;
        std::cout << "text: commandprefix [ARGS]" << std::endl;
        std::cout << "shortcut: clear, enable, disable, add [ARGS], remove [ARGS], list" << std::endl;
        std::cout << "testing [ARGS]" << std::endl;
        std::cout << "data: get [ARGS], clear" << std::endl;
        std::cout << "saveloop [ARGS]" << std::endl;
        std::cout << "saveonexit [ARGS]" << std::endl;
        std::cout << "checkforupdates [ARGS]" << std::endl;
        return;
    }
    std::cerr << "Unknown command. No given ARGS. Try 'help'" << std::endl;
}

void userDataCommands(){
    getNextCommand();
    if (lastCommandParsed.empty()) {
        std::cerr << "Error: No arguments provided. Try 'help' for a list of commands." << std::endl;
        return;
    }
    if (lastCommandParsed == "get") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cerr << "Error: No arguments provided. Try 'help' for a list of commands." << std::endl;
            return;
        }
        if (lastCommandParsed == "userdata") {
            std::cout << readAndReturnUserDataFile() << std::endl;
            return;
        }
        if (lastCommandParsed == "userhistory") {
            std::ifstream file(USER_COMMAND_HISTORY);
            if (file.is_open()) {
                std::string history((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();
                std::cout << history << std::endl;
            } else {
                std::cerr << "Error: Unable to read the user history file at " << USER_COMMAND_HISTORY << std::endl;
            }
            return;
        }
        if (lastCommandParsed == "all") {
            std::cout << readAndReturnUserDataFile() << std::endl;
            std::ifstream file(USER_COMMAND_HISTORY);
            if (file.is_open()) {
                std::string history((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();
                std::cout << history << std::endl;
            } else {
                std::cerr << "Error: Unable to read the user history file at " << USER_COMMAND_HISTORY << std::endl;
            }
            return;
        }
    }
    if (lastCommandParsed == "clear") {
        std::filesystem::remove(USER_DATA);
        createNewUSER_DATAFile();
        std::cout << "User data file cleared." << std::endl;
        std::filesystem::remove(USER_COMMAND_HISTORY);
        createNewUSER_HISTORYfile();
        std::cout << "User history file cleared." << std::endl;
        return;
    }
    if(lastCommandParsed == "help") {
        std::cout << "Commands: " << std::endl;
        std::cout << "get [ARGS]" << std::endl;
        std::cout << "clear" << std::endl;
        return;
    }
    std::cerr << "Error: Unknown command. Try 'help' for a list of commands." << std::endl;
}

void startupCommandsHandler() {
    getNextCommand();
    if (lastCommandParsed.empty()) {
        if (!startupCommands.empty()) {
            std::cout << "Startup commands:" << std::endl;
            for (const auto& command : startupCommands) {
                std::cout << command << std::endl;
            }
        } else {
            std::cout << "No startup commands." << std::endl;
        }
        return;
    }
    if (lastCommandParsed == "add") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        startupCommands.push_back(lastCommandParsed);
        std::cout << "Command added to startup commands." << std::endl;
        return;
    }
    if (lastCommandParsed == "remove") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        std::vector<std::string> newStartupCommands;
        bool removed = false;
        for (const auto& command : startupCommands) {
            if (command != lastCommandParsed) {
                newStartupCommands.push_back(command);
            } else {
                removed = true;
                std::cout << "Command removed from startup commands." << std::endl;
            }
        }
        if (!removed) {
            std::cout << "Command not found in startup commands." << std::endl;
        }
        startupCommands = newStartupCommands;
        return;
    }
    if (lastCommandParsed == "clear") {
        startupCommands.clear();
        std::cout << "Startup commands cleared." << std::endl;
        return;
    }
    if (lastCommandParsed == "enable") {
        startCommandsOn = true;
        std::cout << "Startup commands enabled." << std::endl;
        return;
    }
    if (lastCommandParsed == "disable") {
        startCommandsOn = false;
        std::cout << "Startup commands disabled." << std::endl;
        return;
    }
    if (lastCommandParsed == "list") {
        if (!startupCommands.empty()) {
            std::cout << "Startup commands:" << std::endl;
            for (const auto& command : startupCommands) {
                std::cout << command << std::endl;
            }
        } else {
            std::cout << "No startup commands." << std::endl;
        }
        return;
    }
    if (lastCommandParsed == "runall") {
        if (!startupCommands.empty()) {
            std::cout << "Running startup commands..." << std::endl;
            for (const auto& command : startupCommands) {
                commandParser(commandPrefix + command);
            }
        } else {
            std::cout << "No startup commands." << std::endl;
        }
        return;
    }
    std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
}

void shortcutCommands() {
    getNextCommand();
    if (lastCommandParsed.empty()) {
        if (!shortcuts.empty()) {
            std::cout << "Shortcuts:" << std::endl;
            for (const auto& [key, value] : shortcuts) {
                std::cout << key + " = " + value << std::endl;
            }
        } else {
            std::cout << "No shortcuts." << std::endl;
        }
        if(!multiScriptShortcuts.empty()){
            std::cout << "Multi-Script Shortcuts:" << std::endl;
            for (const auto& [key, value] : multiScriptShortcuts) {
                std::cout << key + " = ";
                for(const auto& command : value){
                    std::cout << "'"+command + "' ";
                }
                std::cout << std::endl;
            }
        } else {
            std::cout << "No multi-script shortcuts." << std::endl;
        }
        return;
    }
    if (lastCommandParsed == "clear") {
        shortcuts.clear();
        std::cout << "Shortcuts cleared." << std::endl;
        return;
    }
    if (lastCommandParsed == "enable") {
        shotcutsEnabled = true;
        std::cout << "Shortcuts enabled." << std::endl;
        return;
    }
    if (lastCommandParsed == "disable") {
        shotcutsEnabled = false;
        std::cout << "Shortcuts disabled." << std::endl;
        return;
    }
    if(lastCommandParsed == "mm"){
        multiScriptShortcutCommands();
        return;
    }
    if (lastCommandParsed == "add") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        std::string shortcut = lastCommandParsed;
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        std::string command = lastCommandParsed;
        shortcuts[shortcut] = command;
        std::cout << "Shortcut added." << std::endl;
        return;
    }
    if (lastCommandParsed == "remove") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        if(shortcuts.find(lastCommandParsed) == shortcuts.end()){
            std::cout << "Shortcut not found." << std::endl;
            return;
        }
        shortcuts.erase(lastCommandParsed);
        std::cout << "Shortcut removed." << std::endl;
        return;
    }
    if (lastCommandParsed == "list") {
        if (!shortcuts.empty()) {
            std::cout << "Shortcuts:" << std::endl;
            for (const auto& [key, value] : shortcuts) {
                std::cout << key + " = " + value << std::endl;
            }
        } else {
            std::cout << "No shortcuts." << std::endl;
        }
        if(!multiScriptShortcuts.empty()){
            std::cout << "Multi-Script Shortcuts:" << std::endl;
            for (const auto& [key, value] : multiScriptShortcuts) {
                std::cout << key + " = ";
                for(const auto& command : value){
                    std::cout << "'"+command + "' ";
                }
                std::cout << std::endl;
            }
        } else {
            std::cout << "No multi-script shortcuts." << std::endl;
        }
        return;
    }
    std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
}

void multiScriptShortcutCommands(){
    getNextCommand();
    if (lastCommandParsed.empty()) {
        std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
        return;
    }
    if(lastCommandParsed == "add"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        std::string shortcut = lastCommandParsed;
        std::vector<std::string> commands;
        getNextCommand();
        while(!lastCommandParsed.empty()){
            commands.push_back(lastCommandParsed);
            getNextCommand();
        }
        if(commands.empty()){
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        multiScriptShortcuts[shortcut] = commands;
        std::cout << "Multi-Script Shortcut added." << std::endl;
        return;
    }
    if(lastCommandParsed == "remove"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
            return;
        }
        if(multiScriptShortcuts.find(lastCommandParsed) == multiScriptShortcuts.end()){
            std::cout << "Multi-Script Shortcut not found." << std::endl;
            return;
        }
        multiScriptShortcuts.erase(lastCommandParsed);
        std::cout << "Multi-Script Shortcut removed." << std::endl;
        return;
    }
    std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
}

void textCommands() {
    getNextCommand();
    if (lastCommandParsed.empty()) {
        std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
        return;
    }
    if (lastCommandParsed == "commandprefix") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Command prefix is currently " + commandPrefix << std::endl;
            return;
        }
        if (lastCommandParsed.length() > 1) {
            std::cout << "Invalid command prefix. Must be a single character." << std::endl;
            return;
        } else if (lastCommandParsed == " ") {
            std::cout << "Invalid command prefix. Must not be a space." << std::endl;
            return;
        }
        commandPrefix = lastCommandParsed;
        std::cout << "Command prefix set to " + commandPrefix << std::endl;
        return;
    }
    if(lastCommandParsed == "displayfullpath"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Display whole path is currently " << (terminal.isDisplayWholePath() ? "enabled." : "disabled.") << std::endl;
            return;
        }
        if(lastCommandParsed == "enable"){
            terminal.setDisplayWholePath(true);
            std::cout << "Display whole path enabled." << std::endl;
            return;
        }
        if(lastCommandParsed == "disable"){
            terminal.setDisplayWholePath(false);
            std::cout << "Display whole path disabled." << std::endl;
            return;
        }
    }
    if(lastCommandParsed == "defaultentry"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "Default text entry is currently " << (defaultTextEntryOnAI ? "AI." : "terminal.") << std::endl;
            return;
        }
        if(lastCommandParsed == "ai"){
            defaultTextEntryOnAI = true;
            std::cout << "Default text entry set to AI." << std::endl;
            return;
        }
        if(lastCommandParsed == "terminal"){
            defaultTextEntryOnAI = false;
            std::cout << "Default text entry set to terminal." << std::endl;
            return;
        }
    }
    std::cout << "Unknown command. No given ARGS. Try 'help'" << std::endl;
}

void getNextCommand() {
    if (!commandsQueue.empty()) {
        lastCommandParsed = commandsQueue.front();
        commandsQueue.pop();
        if (TESTING) {
            std::cout << "Processed Command: " << lastCommandParsed << std::endl;
        }
    } else {
        lastCommandParsed = "";
    }
}

void aiSettingsCommands() {
    getNextCommand();
    if (lastCommandParsed.empty()) {
        defaultTextEntryOnAI = true;
        showChatHistory();
        return;
    }
    if (lastCommandParsed == "log") {
        std::string lastChatSent = openAIPromptEngine.getLastPromptUsed();
        std::string lastChatReceived = openAIPromptEngine.getLastResponseReceived();
        std::string fileName = (DATA_DIRECTORY / ("OpenAPI_Chat_" + std::to_string(time(nullptr)) + ".txt")).string();
        std::ofstream file(fileName);
        if (file.is_open()) {
            file << "Chat Sent: " << lastChatSent << "\n";
            file << "Chat Received: " << lastChatReceived << "\n";
            file.close();
            std::cout << "Chat log saved to " << fileName << std::endl;
        } else {
            std::cerr << "Error: Unable to create the chat log file at " << fileName << std::endl;
            return;
        }
    }
    if (lastCommandParsed == "apikey") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << openAIPromptEngine.getAPIKey() << std::endl;
            return;
        }
        if (lastCommandParsed == "set") {
            getNextCommand();
            if (lastCommandParsed.empty()) {
                std::cerr << "Error: No API key provided. Try 'help' for a list of commands." << std::endl;
                return;
            }
            openAIPromptEngine.setAPIKey(lastCommandParsed);
            if (openAIPromptEngine.testAPIKey(openAIPromptEngine.getAPIKey())) {
                std::cout << "OpenAI API key set successfully." << std::endl;
                return;
            } else {
                std::cerr << "Error: Invalid API key." << std::endl;
                return;
            }
        }
        if (lastCommandParsed == "get") {
            std::cout << openAIPromptEngine.getAPIKey() << std::endl;
            return;
        }
        std::cerr << "Error: Unknown command. Try 'help' for a list of commands." << std::endl;
        return;
    }
    if (lastCommandParsed == "chat") {
        aiChatCommands();
        return;
    }
    if (lastCommandParsed == "get") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cerr << "Error: No arguments provided. Try 'help' for a list of commands." << std::endl;
            return;
        }
        std::cout << openAIPromptEngine.getResponseData(lastCommandParsed) << std::endl;
        return;
    }
    if (lastCommandParsed == "dump") {
        std::cout << openAIPromptEngine.getResponseData("all") << std::endl;
        std::cout << openAIPromptEngine.getLastPromptUsed() << std::endl;
        return;
    }
    if (lastCommandParsed == "mode") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "The current assistant mode is " << openAIPromptEngine.getAssistantType() << std::endl;
            return;
        }
        openAIPromptEngine.setAssistantType(lastCommandParsed);
        std::cout << "Assistant mode set to " << lastCommandParsed << std::endl;
        return;
    }
    if (lastCommandParsed == "file") {
        getNextCommand();
        std::vector<std::string> filesAtPath = terminal.getFilesAtCurrentPath();
        if (lastCommandParsed.empty()) {
            std::vector<std::string> activeFiles = openAIPromptEngine.getFiles();
            std::cout << "Active Files: " << std::endl;
            for(const auto& file : activeFiles){
                std::cout << file << std::endl;
            }
            std::cout << "Total characters processed: " << openAIPromptEngine.getFileContents().length() << std::endl;
            std::cout << "Files at current path: " << std::endl;
            for(const auto& file : filesAtPath){
                std::cout << file << std::endl;
            }
            return;
        }
        if (lastCommandParsed == "add"){
            getNextCommand();
            if (lastCommandParsed.empty()) {
                std::cerr << "Error: No file specified. Try 'help' for a list of commands." << std::endl;
                return;
            }
            if (lastCommandParsed == "all"){
                std::cout << "Processed " << openAIPromptEngine.addFiles(filesAtPath) <<  " characters."  << std::endl;
                return;
            }
            std::string fileToAdd = terminal.getFullPathOfFile(lastCommandParsed);
            if(fileToAdd.empty()){
                std::cerr << "Error: File not found." << std::endl;
                return;
            }
            std::cout << "Processed "<<openAIPromptEngine.addFile(fileToAdd) << " characters." << std::endl;
            return;
        }
        if (lastCommandParsed == "remove"){
            getNextCommand();
            if (lastCommandParsed.empty()) {
                std::cerr << "Error: No file specified. Try 'help' for a list of commands." << std::endl;
                return;
            }
            if (lastCommandParsed == "all"){
                openAIPromptEngine.clearFiles();
                return;
            }
            std::string fileToRemove = terminal.getFullPathOfFile(lastCommandParsed);
            if(fileToRemove.empty()){
                std::cerr << "Error: File not found." << std::endl;
                return;
            }
            openAIPromptEngine.removeFile(fileToRemove);
            return;
        }
        if (lastCommandParsed == "active"){
            std::vector<std::string> activeFiles = openAIPromptEngine.getFiles();
            std::cout << "Active Files: " << std::endl;
            for(const auto& file : activeFiles){
                std::cout << file << std::endl;
            }
            std::cout << "Total characters processed: " << openAIPromptEngine.getFileContents().length() << std::endl;
            return;
        }
        if (lastCommandParsed == "available"){
            std::cout << "Files at current path: " << std::endl;
            for(const auto& file : filesAtPath){
                std::cout << file << std::endl;
            }
            return;
        }
        if(lastCommandParsed == "refresh"){
            openAIPromptEngine.refreshFiles();
            std::cout << "Files refreshed." << std::endl;
            return;
        }
        if(lastCommandParsed == "clear"){
            openAIPromptEngine.clearFiles();
            std::cout << "Files cleared." << std::endl;
            return;
        }
        std::cerr << "Error: Unknown command. Try 'help' for a list of commands." << std::endl;
        return;
    }
    if(lastCommandParsed == "directory"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "The current directory is " << openAIPromptEngine.getSaveDirectory() << std::endl;
            return;
        }
        if(lastCommandParsed == "set") {
            openAIPromptEngine.setSaveDirectory(terminal.getCurrentFilePath());
            std::cout << "Directory set to " << terminal.getCurrentFilePath() << std::endl;
            return;
        }
        if(lastCommandParsed == "clear") {
            openAIPromptEngine.setSaveDirectory(".DTT-Data");
            std::cout << "Directory set to default." << std::endl;
            return;
        }
    }
    if(lastCommandParsed == "model"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "The current model is " << openAIPromptEngine.getModel() << std::endl;
            return;
        }
        openAIPromptEngine.setModel(lastCommandParsed);
        std::cout << "Model set to " << lastCommandParsed << std::endl;
        return;
    }
    if(lastCommandParsed == "rejectchanges"){
        openAIPromptEngine.rejectChanges();
        std::cout << "Changes rejected." << std::endl;
        return;
    }
    if(lastCommandParsed == "timeoutflag"){
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cout << "The current timeout flag is " << openAIPromptEngine.getTimeoutFlagSeconds() << std::endl;
            return;
        }
        openAIPromptEngine.setTimeoutFlagSeconds(std::stoi(lastCommandParsed));
        std::cout << "Timeout flag set to " << lastCommandParsed << " seconds."<< std::endl;
        return;
    }
    if (lastCommandParsed == "help") {
        std::cout << "Commands: " << std::endl;
        std::cout << "log" << std::endl;
        std::cout << "apikey: set [ARGS], get" << std::endl;
        std::cout << "chat: [ARGS]" << std::endl;
        std::cout << "get: [ARGS]" << std::endl;
        std::cout << "dump" << std::endl;
        std::cout << "mode: [ARGS]" << std::endl;
        std::cout << "file: add [ARGS], remove [ARGS], active, available, refresh" << std::endl;
        std::cout << "directory: set, clear" << std::endl;
        std::cout << "model: [ARGS]" << std::endl;
        std::cout << "rejectchanges" << std::endl;
        std::cout << "timeoutflag: [ARGS]" << std::endl;
        return;
    }
    std::cerr << "Error: Unknown command. Try 'help' for a list of commands." << std::endl;
}

void aiChatCommands() {
    getNextCommand();
    if (lastCommandParsed.empty()) {
        std::cerr << "Error: No arguments provided. Try 'help' for a list of commands." << std::endl;
        return;
    }
    if (lastCommandParsed == "history") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cerr << "Error: No arguments provided. Try 'help' for a list of commands." << std::endl;
            return;
        }
        if (lastCommandParsed == "clear") {
            openAIPromptEngine.clearChatCache();
            savedChatCache.clear();
            openAIPromptEngine.clearAllCachedTokens();
            std::cout << "Chat history cleared." << std::endl;
            return;
        }
    }
    if (lastCommandParsed == "cache") {
        getNextCommand();
        if (lastCommandParsed.empty()) {
            std::cerr << "Error: No arguments provided. Try 'help' for a list of commands." << std::endl;
            return;
        }
        if (lastCommandParsed == "enable") {
            openAIPromptEngine.setCacheTokens(true);
            std::cout << "Cache tokens enabled." << std::endl;
            return;
        }
        if (lastCommandParsed == "disable") {
            openAIPromptEngine.setCacheTokens(false);
            std::cout << "Cache tokens disabled." << std::endl;
            return;
        }
        if (lastCommandParsed == "clear") {
            openAIPromptEngine.clearAllCachedTokens();
            std::cout << "Chat history cleared." << std::endl;
            return;
        }
    }
    if (lastCommandParsed == "help") {
        std::cout << "Commands: " << std::endl;
        std::cout << "history: clear" << std::endl;
        std::cout << "cache: enable, disable, clear" << std::endl;
        return;
    }
    for(int i = 0; i < commandsQueue.size(); i++){
        lastCommandParsed += " " + commandsQueue.front();
        commandsQueue.pop();
    }
    std::cout << "Sent message to GPT: " << lastCommandParsed << std::endl;
    chatProcess(lastCommandParsed);
    return;
}

void chatProcess(const std::string& message) {
    if (message.empty()) {
        return;
    }
    if(message == "exit"){
        exitFlag = true;
        return;
    }
    if(message == "clear"){
        sendTerminalCommand("clear");
        return;
    }
    if (openAIPromptEngine.getAPIKey().empty()) {
        std::cerr << "Error: No OpenAPI key set. Please set the API key using 'ai apikey set [KEY]'." << std::endl;
        return;
    }
    std::string response = openAIPromptEngine.chatGPT(message,false);
    std::cout << "ChatGPT:\n" << response << std::endl;
}

void showChatHistory() {
    if (!openAIPromptEngine.getChatCache().empty()) {
        std::cout << "Chat history:" << std::endl;
        for (const auto& message : openAIPromptEngine.getChatCache()) {
            std::cout << message << std::endl;
        }
    }
}

bool checkForUpdate() {
    std::cout << "Checking for updates..." << std::endl;
    auto isNewerVersion = [](const std::string &latest, const std::string &current) -> bool {
        auto splitVersion = [](const std::string &ver) {
            std::vector<int> parts;
            std::istringstream iss(ver);
            std::string token;
            while (std::getline(iss, token, '.')) {
                parts.push_back(std::stoi(token));
            }
            return parts;
        };
        std::vector<int> latestParts = splitVersion(latest);
        std::vector<int> currentParts = splitVersion(current);
        size_t len = std::max(latestParts.size(), currentParts.size());
        latestParts.resize(len, 0);
        currentParts.resize(len, 0);
        for (size_t i = 0; i < len; i++) {
            if (latestParts[i] > currentParts[i]) return true;
            if (latestParts[i] < currentParts[i]) return false;
        }
        return false;
    };

    std::string command = "curl -s " + updateURL;
    std::string result;
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Unable to execute update check or no internet connection." << std::endl;
        return false;
    }
    char buffer[128];
    while (fgets(buffer, 128, pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);

    try {
        json jsonData = json::parse(result);
        if (jsonData.contains("tag_name")) {
            std::string latestTag = jsonData["tag_name"].get<std::string>();
            if (!latestTag.empty() && latestTag[0] == 'v') {
                latestTag = latestTag.substr(1);
            }
            std::string currentVer = currentVersion;
            if (!currentVer.empty() && currentVer[0] == 'v') {
                currentVer = currentVer.substr(1);
            }
            if (isNewerVersion(latestTag, currentVer)) {
                std::cout << currentVersion << " -> " << latestTag << std::endl;
                return true;
            }
        }
    } catch (std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return false;
}

bool isDownloadURLValid(const std::string& url) {
    std::string command = "curl -Is " + url + " | head -n 1";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return false;
    }
    char buffer[128];
    std::string result;
    while (fgets(buffer, 128, pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result.find("200 OK") != std::string::npos;
}

bool downloadLatestRelease(){
    std::string releaseJson;
    std::string curlCmd = "curl -s " + updateURL;
    FILE* pipe = popen(curlCmd.c_str(), "r");
    if(!pipe){
        std::cerr << "Error: Unable to fetch release information." << std::endl;
        return false;
    }
    char buffer[128];
    while(fgets(buffer, 128, pipe) != nullptr){
        releaseJson += buffer;
    }
    pclose(pipe);
    if(releaseJson.empty()){
        std::cerr << "Error: Empty response when fetching release info." << std::endl;
        return false;
    }
    try {
        json releaseData = json::parse(releaseJson);
        if(!releaseData.contains("assets") || !releaseData["assets"].is_array() || releaseData["assets"].empty()){
            std::cerr << "Error: No assets found in the latest release." << std::endl;
            return false;
        }
        std::string downloadUrl;
        std::string changeLog;
        if(releaseData.contains("body")) {
            changeLog = releaseData["body"].get<std::string>();
        }
        #ifdef _WIN32
        for (const auto& asset : releaseData["assets"]) {
            if (asset["browser_download_url"].get<std::string>().find(".exe") != std::string::npos) {
                downloadUrl = asset["browser_download_url"].get<std::string>();
                break;
            }
        }
        #else
        downloadUrl = releaseData["assets"][0]["browser_download_url"].get<std::string>();
        #endif
        if (downloadUrl.empty() || !isDownloadURLValid(downloadUrl)) {
            std::cerr << "Error: Invalid download URL or no internet connection." << std::endl;
            return false;
        }
        size_t pos = downloadUrl.find_last_of('/');
        std::string filename = (pos != std::string::npos) ? downloadUrl.substr(pos+1) : "latest_release";
        std::string downloadPath = (std::filesystem::current_path() / filename).string();
        std::string downloadCmd = "curl -L -o " + downloadPath + " " + downloadUrl;
        int ret = system(downloadCmd.c_str());
        if(ret == 0){
            std::string chmodCmd = "chmod +x " + downloadPath;
            system(chmodCmd.c_str());
            std::cout << "Downloaded latest release asset to " << downloadPath << std::endl;
            std::cout << "Replacing current running program with updated version..." << std::endl;
            std::string exePath = (std::filesystem::current_path() / "DevToolsTerminal").string();
            #ifdef _WIN32
            exePath += ".exe";
            #endif
            if(std::rename(downloadPath.c_str(), exePath.c_str()) != 0){
                std::cerr << "Error: Failed to replace the current executable." << std::endl;
                return false;
            }
            std::ofstream changelogFile(DATA_DIRECTORY / "CHANGELOG.txt");
            if (changelogFile.is_open()) {
                changelogFile << changeLog;
                changelogFile.close();
            }
            execl(exePath.c_str(), exePath.c_str(), (char*)NULL);
            std::cerr << "Error: Failed to execute the updated program." << std::endl;
            return false;
        } else {
            std::cerr << "Error: Download command failed." << std::endl;
            return false;
        }
    } catch(std::exception &e) {
        std::cerr << "Error parsing release JSON: " << e.what() << std::endl;
        return false;
    }
}

void displayChangeLog(const std::string& changeLog) {
    std::string clickableChangeLog = changeLog;
    size_t pos = 0;
    while ((pos = clickableChangeLog.find("http", pos)) != std::string::npos) {
        size_t end = clickableChangeLog.find_first_of(" \n", pos);
        if (end == std::string::npos) end = clickableChangeLog.length();
        std::string url = clickableChangeLog.substr(pos, end - pos);
        std::string clickableUrl = "\033]8;;" + url + "\033\\" + url + "\033]8;;\033\\";
        clickableChangeLog.replace(pos, url.length(), clickableUrl);
        pos += clickableUrl.length();
    }
    std::cout << "Change Log:" << std::endl;
    std::cout << clickableChangeLog << std::endl;
}