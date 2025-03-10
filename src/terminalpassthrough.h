#ifndef TERMINALPASSTHROUGH_H
#define TERMINALPASSTHROUGH_H
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <regex>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <array>

namespace fs = std::filesystem;

class TerminalPassthrough {
public:
    TerminalPassthrough();

    std::string getTerminalName();
    std::string returnCurrentTerminalPosition();
    int getTerminalCurrentPositionRawLength();
    void printCurrentTerminalPosition();

    std::vector<std::string> getFilesAtCurrentPath();
    std::string getFullPathOfFile(const std::string& file);
    std::string getCurrentFilePath();
    void setDisplayWholePath(bool displayWholePath);
    void toggleDisplayWholePath();
    bool isDisplayWholePath();

    std::thread executeCommand(std::string command);
    void addCommandToHistory(const std::string& command);
    std::string getPreviousCommand();
    std::string getNextCommand();

    std::vector<std::string> getTerminalCacheUserInput();
    std::vector<std::string> getTerminalCacheTerminalOutput();
    void clearTerminalCache();
    std::string returnMostRecentUserInput();
    std::string returnMostRecentTerminalOutput();

private:
    std::string currentDirectory;
    bool displayWholePath;
    std::vector<std::string> terminalCacheUserInput;
    std::vector<std::string> terminalCacheTerminalOutput;
    std::string RED_COLOR_BOLD = "\033[1;31m";
    std::string RESET_COLOR = "\033[0m";
    std::string BLUE_COLOR_BOLD = "\033[1;34m";
    std::string YELLOW_COLOR_BOLD = "\033[1;33m";
    int commandHistoryIndex = -1;
    int terminalCurrentPositionRawLength = 0;

    std::string getCurrentFileName();
    bool isRootPath(const fs::path& path);
};

#endif // TERMINALPASSTHROUGH_H
