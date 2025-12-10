#include "../include/SomeObj.h"
#include "../include/Repository.h"
#include "../include/Utils.h"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <queue>
#include <unordered_map>
#include <climits>

SomeObj::SomeObj() {}

// Initializes a new Gitlite repository
void SomeObj::init() {
    if (Utils::isDirectory(".gitlite")) {
        Utils::exitWithMessage("A Gitlite version-control system already exists in the current directory.");
    }

    // Create .gitlite directory structure
    Utils::createDirectories(".gitlite/objects");
    Utils::createDirectories(".gitlite/refs/heads");
    Utils::createDirectories(".gitlite/refs/remotes");

    // Create initial commit
    std::string initialCommitTime = "Thu Jan 01 00:00:00 1970 +0000";
    std::string initialCommitMessage = "initial commit";

    // Create initial commit with no files
    std::string commitContent = "parent \n";
    commitContent += "timestamp " + std::to_string(0) + "\n";
    commitContent += "message " + initialCommitMessage + "\n";
    commitContent += "files \n";

    std::string commitId = Utils::sha1(commitContent);
    Utils::writeContents(".gitlite/objects/" + commitId, commitContent);

    // Create master branch pointing to initial commit
    Utils::writeContents(".gitlite/refs/heads/master", commitId);

    // Set HEAD to master
    Utils::writeContents(".gitlite/HEAD", "ref: refs/heads/master");
}

// Adds a file to the staging area
void SomeObj::add(const std::string &filename) {
    if (!Utils::exists(filename)) {
        Utils::exitWithMessage("File does not exist.");
    }

    // Read file content
    std::string content = Utils::readContentsAsString(filename);
    std::string blobId = Utils::sha1(content);

    // Store blob if not exists
    std::string blobPath = ".gitlite/objects/" + blobId;
    if (!Utils::exists(blobPath)) {
        Utils::writeContents(blobPath, content);
    }

    // Get current commit to check if file is the same as in current commit
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    auto currentCommitFiles = getFilesInCommit(currentCommitId);
    bool fileInCurrentCommit = currentCommitFiles.find(filename) != currentCommitFiles.end();
    bool sameAsCurrentCommit = false;

    if (fileInCurrentCommit) {
        std::string currentBlobId = currentCommitFiles[filename];
        sameAsCurrentCommit = (currentBlobId == blobId);
    }

    // Check if file is staged for removal
    bool stagedForRemoval = false;
    if (Utils::isDirectory(".gitlite/staging") && Utils::exists(".gitlite/staging/" + filename)) {
        std::string stagedContent = Utils::readContentsAsString(".gitlite/staging/" + filename);
        stagedForRemoval = (stagedContent == "DELETE");
    }

    Utils::createDirectories(".gitlite/staging");

    // If file is the same as in current commit and was staged for removal, unstage it
    if (stagedForRemoval && sameAsCurrentCommit) {
        Utils::restrictedDelete(".gitlite/staging/" + filename);
    }
    // If file is the same as in current commit and not staged for removal, don't stage it
    else if (sameAsCurrentCommit && !stagedForRemoval) {
        // Remove from staging if it exists
        if (Utils::exists(".gitlite/staging/" + filename)) {
            Utils::restrictedDelete(".gitlite/staging/" + filename);
        }
    }
    // Otherwise, stage the file
    else {
        Utils::writeContents(".gitlite/staging/" + filename, blobId);
    }
}

// Commits the staged changes
void SomeObj::commit(const std::string &message) {
    if (message.empty()) {
        Utils::exitWithMessage("Please enter a commit message.");
    }

    // Check if there are staged changes
    if (!Utils::isDirectory(".gitlite/staging") || Utils::plainFilenamesIn(".gitlite/staging").empty()) {
        Utils::exitWithMessage("No changes added to the commit.");
    }

    // Get current commit
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16); // Remove "ref: refs/heads/"
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    // Get current timestamp
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%a %b %d %H:%M:%S %Y %z");
    std::string timestamp = std::to_string(now);

    // Create commit content
    std::string commitContent = "parent " + currentCommitId + "\n";
    commitContent += "timestamp " + timestamp + "\n";
    commitContent += "message " + message + "\n";
    commitContent += "files ";

    // Start with files from current commit
    auto currentCommitFiles = getFilesInCommit(currentCommitId);

    // Apply staged changes
    auto stagedFiles = Utils::plainFilenamesIn(".gitlite/staging");
    for (const auto &file : stagedFiles) {
        std::string blobId = Utils::readContentsAsString(".gitlite/staging/" + file);
        if (blobId == "DELETE") {
            // Remove file from commit
            currentCommitFiles.erase(file);
        } else {
            // Add or update file in commit
            currentCommitFiles[file] = blobId;
        }
    }

    // Build files section from the updated map
    for (const auto &pair : currentCommitFiles) {
        commitContent += pair.first + ":" + pair.second + ";";
    }
    commitContent += "\n";

    // Create commit
    std::string newCommitId = Utils::sha1(commitContent);
    Utils::writeContents(".gitlite/objects/" + newCommitId, commitContent);

    // Update branch reference
    Utils::writeContents(".gitlite/refs/heads/" + currentBranch, newCommitId);

    // Clear staging area
    std::string command = "rm -rf .gitlite/staging";
    system(command.c_str());
}

// Removes a file from the staging area or working directory
void SomeObj::rm(const std::string &filename) {
    bool fileStaged = Utils::exists(".gitlite/staging/" + filename);
    bool fileTracked = false;

    // Check if file is tracked in current commit
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    if (Utils::exists(".gitlite/objects/" + currentCommitId)) {
        std::string commitContent = Utils::readContentsAsString(".gitlite/objects/" + currentCommitId);
        size_t filesPos = commitContent.find("files ");
        if (filesPos != std::string::npos) {
            std::string filesSection = commitContent.substr(filesPos + 6);
            size_t newlinePos = filesSection.find('\n');
            if (newlinePos != std::string::npos) {
                filesSection = filesSection.substr(0, newlinePos);
            }
            if (filesSection.find(filename + ":") != std::string::npos) {
                fileTracked = true;
            }
        }
    }

    if (!fileStaged && !fileTracked) {
        Utils::exitWithMessage("No reason to remove the file.");
    }

    // If file is staged but not tracked, unstage it
    if (fileStaged && !fileTracked) {
        Utils::restrictedDelete(".gitlite/staging/" + filename);
        return;
    }

    // If file is tracked, stage it for removal
    Utils::createDirectories(".gitlite/staging");
    Utils::writeContents(".gitlite/staging/" + filename, "DELETE");

    // Remove from working directory if it exists
    if (Utils::exists(filename)) {
        Utils::restrictedDelete(filename);
    }
}

// Displays the commit history
void SomeObj::log() {
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    while (!currentCommitId.empty()) {
        std::string commitPath = ".gitlite/objects/" + currentCommitId;
        if (!Utils::exists(commitPath)) {
            break;
        }

        std::string commitContent = Utils::readContentsAsString(commitPath);

        // Parse commit information
        std::string parent, timestamp, message;
        size_t pos = 0;

        pos = commitContent.find("parent ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            parent = commitContent.substr(pos + 7, end - pos - 7);
        }

        pos = commitContent.find("timestamp ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            std::string timestampStr = commitContent.substr(pos + 10, end - pos - 10);
            std::time_t ts = std::stoll(timestampStr);
            auto tm = *std::localtime(&ts);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%a %b %d %H:%M:%S %Y %z");
            timestamp = oss.str();
        }

        pos = commitContent.find("message ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            message = commitContent.substr(pos + 8, end - pos - 8);
        }

        // Print commit information
        std::cout << "===" << std::endl;
        std::cout << "commit " << currentCommitId << std::endl;

        // Check if this is a merge commit (has two parents)
        size_t spacePos = parent.find(' ');
        if (spacePos != std::string::npos) {
            std::string firstParent = parent.substr(0, spacePos);
            std::string secondParent = parent.substr(spacePos + 1);
            std::cout << "Merge: " << firstParent.substr(0, 7) << " " << secondParent.substr(0, 7) << std::endl;
        }

        std::cout << "Date: " << timestamp << std::endl;
        std::cout << message << std::endl
                  << std::endl;

        // For merge commits, follow first parent only
        if (spacePos != std::string::npos) {
            currentCommitId = parent.substr(0, spacePos);
        } else {
            currentCommitId = parent;
        }
    }
}

// Displays all commits ever made
void SomeObj::globalLog() {
    auto commitFiles = Utils::plainFilenamesIn(".gitlite/objects");

    for (const auto &commitId : commitFiles) {
        std::string commitPath = ".gitlite/objects/" + commitId;
        std::string commitContent = Utils::readContentsAsString(commitPath);

        // Skip if not a commit (blobs don't have "parent " prefix)
        if (commitContent.substr(0, 7) != "parent ") {
            continue;
        }

        // Parse commit information
        std::string timestamp, message;
        size_t pos = 0;

        pos = commitContent.find("timestamp ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            std::string timestampStr = commitContent.substr(pos + 10, end - pos - 10);
            std::time_t ts = std::stoll(timestampStr);
            auto tm = *std::localtime(&ts);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%a %b %d %H:%M:%S %Y %z");
            timestamp = oss.str();
        }

        pos = commitContent.find("message ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            message = commitContent.substr(pos + 8, end - pos - 8);
        }

        // Print commit information
        std::cout << "===" << std::endl;
        std::cout << "commit " << commitId << std::endl;
        std::cout << "Date: " << timestamp << std::endl;
        std::cout << message << std::endl
                  << std::endl;
    }
}

// Finds commits with the given message
void SomeObj::find(const std::string &commitMessage) {
    bool found = false;
    auto commitFiles = Utils::plainFilenamesIn(".gitlite/objects");

    for (const auto &commitId : commitFiles) {
        std::string commitPath = ".gitlite/objects/" + commitId;
        std::string commitContent = Utils::readContentsAsString(commitPath);

        // Skip if not a commit
        if (commitContent.substr(0, 7) != "parent ") {
            continue;
        }

        // Check if commit message matches
        size_t pos = commitContent.find("message ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            std::string message = commitContent.substr(pos + 8, end - pos - 8);
            if (message == commitMessage) {
                std::cout << commitId << std::endl;
                found = true;
            }
        }
    }

    if (!found) {
        Utils::exitWithMessage("Found no commit with that message.");
    }
}

// Restores a file from the current commit
void SomeObj::checkoutFile(const std::string &filename) {
    // Get current commit
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    checkoutFileInCommit(currentCommitId, filename);
}

void SomeObj::checkoutFileInCommit(const std::string &commitId, const std::string &filename) {
    // Find full commit ID from short ID
    std::string fullCommitId = commitId;
    if (commitId.length() < 40) {
        auto commitFiles = Utils::plainFilenamesIn(".gitlite/objects");
        bool found = false;

        for (const auto &id : commitFiles) {
            if (id.substr(0, commitId.length()) == commitId) {
                fullCommitId = id;
                found = true;
                break;
            }
        }

        if (!found) {
            Utils::exitWithMessage("No commit with that id exists.");
        }
    }

    // Check if commit exists
    std::string commitPath = ".gitlite/objects/" + fullCommitId;
    if (!Utils::exists(commitPath)) {
        Utils::exitWithMessage("No commit with that id exists.");
    }

    std::string commitContent = Utils::readContentsAsString(commitPath);

    // Find file in commit
    size_t filesPos = commitContent.find("files ");
    if (filesPos == std::string::npos) {
        Utils::exitWithMessage("File does not exist in that commit.");
    }

    std::string filesSection = commitContent.substr(filesPos + 6);
    size_t newlinePos = filesSection.find('\n');
    if (newlinePos != std::string::npos) {
        filesSection = filesSection.substr(0, newlinePos);
    }

    // Parse files section
    size_t filePos = filesSection.find(filename + ":");
    if (filePos == std::string::npos) {
        Utils::exitWithMessage("File does not exist in that commit.");
    }

    size_t colonPos = filesSection.find(':', filePos);
    size_t semicolonPos = filesSection.find(';', colonPos);
    std::string blobId = filesSection.substr(colonPos + 1, semicolonPos - colonPos - 1);

    // Restore file content
    std::string blobPath = ".gitlite/objects/" + blobId;
    std::string fileContent = Utils::readContentsAsString(blobPath);
    Utils::writeContents(filename, fileContent);
}

// Switches to the specified branch
void SomeObj::checkoutBranch(const std::string &branchName) {
    // Check if branch exists
    std::string branchPath = ".gitlite/refs/heads/" + branchName;
    if (!Utils::exists(branchPath)) {
        Utils::exitWithMessage("No such branch exists.");
    }

    // Get current branch
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);

    if (currentBranch == branchName) {
        Utils::exitWithMessage("No need to checkout the current branch.");
    }

    // Get commit IDs
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);
    std::string targetCommitId = Utils::readContentsAsString(branchPath);

    // Check for untracked files that would be overwritten
    auto currentFiles = Utils::plainFilenamesIn(".");
    auto targetCommitFiles = getFilesInCommit(targetCommitId);
    auto currentCommitFiles = getFilesInCommit(currentCommitId);

    for (const auto &file : currentFiles) {
        if (file == ".gitlite" || file.substr(0, 9) == ".gitlite/") {
            continue;
        }

        // Check if file exists in working directory and would be overwritten by checkout
        // AND it's not currently tracked in the current branch
        if (targetCommitFiles.find(file) != targetCommitFiles.end() &&
            currentCommitFiles.find(file) == currentCommitFiles.end()) {
            // Also check if it's not staged for addition
            bool isStaged = Utils::isDirectory(".gitlite/staging") &&
                            Utils::exists(".gitlite/staging/" + file) &&
                            Utils::readContentsAsString(".gitlite/staging/" + file) != "DELETE";

            if (!isStaged) {
                Utils::exitWithMessage("There is an untracked file in the way; delete it, or add and commit it first.");
            }
        }
    }

    // Restore files from target branch
    for (const auto &pair : targetCommitFiles) {
        std::string blobId = pair.second;
        std::string blobPath = ".gitlite/objects/" + blobId;
        std::string content = Utils::readContentsAsString(blobPath);
        Utils::writeContents(pair.first, content);
    }

    // Delete files that are tracked in current branch but not in target branch
    for (const auto &pair : currentCommitFiles) {
        if (targetCommitFiles.find(pair.first) == targetCommitFiles.end()) {
            Utils::restrictedDelete(pair.first);
        }
    }

    // Update HEAD
    Utils::writeContents(".gitlite/HEAD", "ref: refs/heads/" + branchName);

    // Clear staging area
    if (Utils::isDirectory(".gitlite/staging")) {
        std::string command = "rm -rf .gitlite/staging";
        system(command.c_str());
    }
}

// Displays the status of the repository
void SomeObj::status() {
    // Get current branch
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);

    // === Branches ===
    std::cout << "=== Branches ===" << std::endl;
    auto branches = Utils::plainFilenamesIn(".gitlite/refs/heads");
    std::sort(branches.begin(), branches.end());

    for (const auto &branch : branches) {
        if (branch == currentBranch) {
            std::cout << "*" << branch << std::endl;
        } else {
            std::cout << branch << std::endl;
        }
    }

    // === Staged Files ===
    std::cout << std::endl
              << "=== Staged Files ===" << std::endl;
    if (Utils::isDirectory(".gitlite/staging")) {
        auto stagedFiles = Utils::plainFilenamesIn(".gitlite/staging");
        std::sort(stagedFiles.begin(), stagedFiles.end());

        for (const auto &file : stagedFiles) {
            std::string content = Utils::readContentsAsString(".gitlite/staging/" + file);
            if (content != "DELETE") {
                std::cout << file << std::endl;
            }
        }
    }

    // === Removed Files ===
    std::cout << std::endl
              << "=== Removed Files ===" << std::endl;
    if (Utils::isDirectory(".gitlite/staging")) {
        auto stagedFiles = Utils::plainFilenamesIn(".gitlite/staging");
        std::sort(stagedFiles.begin(), stagedFiles.end());

        for (const auto &file : stagedFiles) {
            std::string content = Utils::readContentsAsString(".gitlite/staging/" + file);
            if (content == "DELETE") {
                std::cout << file << std::endl;
            }
        }
    }

    // === Modifications Not Staged For Commit ===
    std::cout << std::endl
              << "=== Modifications Not Staged For Commit ===" << std::endl;

    std::map<std::string, std::string> modifications;
    
    auto workingFiles = Utils::plainFilenamesIn(".");
    std::set<std::string> workingSet;
    for(const auto& f : workingFiles) {
        if (f != ".gitlite" && f.find(".gitlite/") != 0) {
            workingSet.insert(f);
        }
    }
    
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);
    auto trackedFiles = getFilesInCommit(currentCommitId);
    
    std::vector<std::string> stagedFiles;
    if (Utils::isDirectory(".gitlite/staging")) {
        stagedFiles = Utils::plainFilenamesIn(".gitlite/staging");
    }
    std::set<std::string> stagedSet(stagedFiles.begin(), stagedFiles.end());
    
    std::set<std::string> allFiles;
    for(const auto& f : workingSet) allFiles.insert(f);
    for(const auto& p : trackedFiles) allFiles.insert(p.first);
    for(const auto& f : stagedFiles) allFiles.insert(f);
    
    for(const auto& file : allFiles) {
        bool inWorking = workingSet.count(file);
        bool inTracked = trackedFiles.count(file);
        bool inStaged = stagedSet.count(file);
        
        std::string stagedContent = "";
        if (inStaged) {
            stagedContent = Utils::readContentsAsString(".gitlite/staging/" + file);
        }
        
        if (inWorking && inTracked && !inStaged) {
            std::string workingContent = Utils::readContentsAsString(file);
            std::string trackedBlob = trackedFiles[file];
            if (Utils::exists(".gitlite/objects/" + trackedBlob)) {
                std::string trackedContent = Utils::readContentsAsString(".gitlite/objects/" + trackedBlob);
                if (workingContent != trackedContent) {
                    modifications[file] = "modified";
                }
            }
        } else if (inWorking && inStaged && stagedContent != "DELETE") {
            std::string workingContent = Utils::readContentsAsString(file);
            std::string stagedBlob = stagedContent;
            if (Utils::exists(".gitlite/objects/" + stagedBlob)) {
                std::string stagedBlobContent = Utils::readContentsAsString(".gitlite/objects/" + stagedBlob);
                if (workingContent != stagedBlobContent) {
                    modifications[file] = "modified";
                }
            }
        } else if (!inWorking && inStaged && stagedContent != "DELETE") {
            modifications[file] = "deleted";
        } else if (!inWorking && !inStaged && inTracked) {
            modifications[file] = "deleted";
        }
    }
    
    for(const auto& p : modifications) {
        std::cout << p.first << " (" << p.second << ")" << std::endl;
    }

    // === Untracked Files ===
    std::cout << std::endl
              << "=== Untracked Files ===" << std::endl;
    {
        auto workingFiles = Utils::plainFilenamesIn(".");
        std::sort(workingFiles.begin(), workingFiles.end());

        // Get current commit files
        std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);
        auto trackedFiles = getFilesInCommit(currentCommitId);

        for (const auto &file : workingFiles) {
            if (file == ".gitlite" || file.substr(0, 9) == ".gitlite/") {
                continue;
            }

            // Check if file is not staged and not tracked
            bool isStaged = Utils::exists(".gitlite/staging/" + file);
            bool isTracked = trackedFiles.find(file) != trackedFiles.end();

            if (!isStaged && !isTracked) {
                std::cout << file << std::endl;
            }
        }
    }
}

// Creates a new branch
void SomeObj::branch(const std::string &branchName) {
    std::string branchPath = ".gitlite/refs/heads/" + branchName;
    if (Utils::exists(branchPath)) {
        Utils::exitWithMessage("A branch with that name already exists.");
    }

    // Get current commit
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    // Create new branch pointing to current commit
    Utils::writeContents(branchPath, currentCommitId);
}

// Deletes the specified branch
void SomeObj::rmBranch(const std::string &branchName) {
    std::string branchPath = ".gitlite/refs/heads/" + branchName;
    if (!Utils::exists(branchPath)) {
        Utils::exitWithMessage("A branch with that name does not exist.");
    }

    // Check if it's the current branch
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);

    if (currentBranch == branchName) {
        Utils::exitWithMessage("Cannot remove the current branch.");
    }

    Utils::restrictedDelete(branchPath);
}

// Resets the current branch to the specified commit
void SomeObj::reset(const std::string &commitId) {
    // Find full commit ID
    std::string fullCommitId = commitId;
    if (commitId.length() < 40) {
        auto commitFiles = Utils::plainFilenamesIn(".gitlite/objects");
        bool found = false;

        for (const auto &id : commitFiles) {
            if (id.substr(0, commitId.length()) == commitId) {
                fullCommitId = id;
                found = true;
                break;
            }
        }

        if (!found) {
            Utils::exitWithMessage("No commit with that id exists.");
        }
    }

    // Check if commit exists
    std::string commitPath = ".gitlite/objects/" + fullCommitId;
    if (!Utils::exists(commitPath)) {
        Utils::exitWithMessage("No commit with that id exists.");
    }

    // Get current branch and commit
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    // Check for untracked files that would be overwritten
    auto currentFiles = Utils::plainFilenamesIn(".");
    auto targetCommitFiles = getFilesInCommit(fullCommitId);
    auto currentCommitFiles = getFilesInCommit(currentCommitId);

    for (const auto &file : currentFiles) {
        if (file == ".gitlite" || file.substr(0, 9) == ".gitlite/") {
            continue;
        }

        // Check if file exists in working directory and would be overwritten by reset
        // AND it's not currently tracked in current branch
        if (targetCommitFiles.find(file) != targetCommitFiles.end() &&
            currentCommitFiles.find(file) == currentCommitFiles.end()) {
            // Also check if it's not staged for addition
            bool isStaged = Utils::isDirectory(".gitlite/staging") &&
                            Utils::exists(".gitlite/staging/" + file) &&
                            Utils::readContentsAsString(".gitlite/staging/" + file) != "DELETE";

            if (!isStaged) {
                Utils::exitWithMessage("There is an untracked file in the way; delete it, or add and commit it first.");
            }
        }
    }

    // Restore files from target commit
    for (const auto &pair : targetCommitFiles) {
        std::string blobId = pair.second;
        std::string blobPath = ".gitlite/objects/" + blobId;
        std::string content = Utils::readContentsAsString(blobPath);
        Utils::writeContents(pair.first, content);
    }

    // Delete files that are tracked in current commit but not in target commit
    for (const auto &pair : currentCommitFiles) {
        if (targetCommitFiles.find(pair.first) == targetCommitFiles.end()) {
            Utils::restrictedDelete(pair.first);
        }
    }

    // Update branch pointer
    Utils::writeContents(".gitlite/refs/heads/" + currentBranch, fullCommitId);

    // Clear staging area
    if (Utils::isDirectory(".gitlite/staging")) {
        std::string command = "rm -rf .gitlite/staging";
        system(command.c_str());
    }
}

// Merges the specified branch into the current branch
void SomeObj::merge(const std::string &branchName) {
    if (!Utils::isDirectory(".gitlite")) {
        Utils::exitWithMessage("Not in an initialized Gitlite directory.");
    }

    std::string branchPath = ".gitlite/refs/heads/" + branchName;
    if (!Utils::exists(branchPath)) {
        Utils::exitWithMessage("A branch with that name does not exist.");
    }

    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    if (branchName == currentBranch) {
        Utils::exitWithMessage("Cannot merge a branch with itself.");
    }

    if (Utils::isDirectory(".gitlite/staging") && !Utils::plainFilenamesIn(".gitlite/staging").empty()) {
        Utils::exitWithMessage("You have uncommitted changes.");
    }

    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);
    std::string givenCommitId = Utils::readContentsAsString(branchPath);
    std::string splitPointId = findSplitPoint(currentCommitId, givenCommitId);

    if (splitPointId == givenCommitId) {
        std::cout << "Given branch is an ancestor of the current branch." << std::endl;
        return;
    }

    if (splitPointId == currentCommitId) {
        reset(givenCommitId);
        std::cout << "Current branch fast-forwarded." << std::endl;
        return;
    }

    auto currentCommitFiles = getFilesInCommit(currentCommitId);
    auto givenCommitFiles = getFilesInCommit(givenCommitId);
    auto splitPointFiles = getFilesInCommit(splitPointId);

    auto workingFiles = Utils::plainFilenamesIn(".");
    for (const auto &file : workingFiles) {
        if (file == ".gitlite" || file.rfind(".gitlite/", 0) == 0) {
            continue;
        }
        bool trackedInCurrent = currentCommitFiles.find(file) != currentCommitFiles.end();
        bool stagedForAdd = Utils::isDirectory(".gitlite/staging") &&
                            Utils::exists(".gitlite/staging/" + file) &&
                            Utils::readContentsAsString(".gitlite/staging/" + file) != "DELETE";
        bool willWriteFromGiven = givenCommitFiles.find(file) != givenCommitFiles.end();
        if (!trackedInCurrent && !stagedForAdd && willWriteFromGiven) {
            Utils::exitWithMessage("There is an untracked file in the way; delete it, or add and commit it first.");
        }
    }

    if (Utils::isDirectory(".gitlite/staging")) {
        std::string cmd = "rm -rf .gitlite/staging";
        system(cmd.c_str());
    }
    Utils::createDirectories(".gitlite/staging");

    auto ensureBlob = [](const std::string &content) {
        std::string blobId = Utils::sha1(content);
        std::string blobPath = ".gitlite/objects/" + blobId;
        if (!Utils::exists(blobPath)) {
            Utils::writeContents(blobPath, content);
        }
        return blobId;
    };

    auto isModified = [](const std::map<std::string, std::string> &branchFiles,
                         const std::map<std::string, std::string> &splitFiles,
                         const std::string &name) {
        bool inSplit = splitFiles.find(name) != splitFiles.end();
        bool inBranch = branchFiles.find(name) != branchFiles.end();
        if (!inSplit) return inBranch;
        if (!inBranch) return true;
        return splitFiles.at(name) != branchFiles.at(name);
    };

    std::set<std::string> allFiles;
    for (const auto &p : splitPointFiles) allFiles.insert(p.first);
    for (const auto &p : currentCommitFiles) allFiles.insert(p.first);
    for (const auto &p : givenCommitFiles) allFiles.insert(p.first);

    bool hasConflicts = false;
    for (const auto &name : allFiles) {
        bool inSplit = splitPointFiles.find(name) != splitPointFiles.end();
        bool inCurrent = currentCommitFiles.find(name) != currentCommitFiles.end();
        bool inGiven = givenCommitFiles.find(name) != givenCommitFiles.end();

        std::string splitBlob = inSplit ? splitPointFiles.at(name) : "";
        std::string curBlob = inCurrent ? currentCommitFiles.at(name) : "";
        std::string givBlob = inGiven ? givenCommitFiles.at(name) : "";

        bool modCur = isModified(currentCommitFiles, splitPointFiles, name);
        bool modGiv = isModified(givenCommitFiles, splitPointFiles, name);

        auto stageBlobFromGiven = [&](const std::string &blob) {
            std::string content = Utils::readContentsAsString(".gitlite/objects/" + blob);
            Utils::writeContents(name, content);
            Utils::writeContents(".gitlite/staging/" + name, blob);
        };

        bool handled = false;

        if (inSplit) {
            if (inCurrent && inGiven) {
                if (!modCur && modGiv) {
                    stageBlobFromGiven(givBlob);
                    handled = true;
                } else if (modCur && !modGiv) {
                    handled = true; // keep current
                } else if (curBlob == givBlob) {
                    handled = true; // same change
                }
            } else if (inCurrent && !inGiven) {
                if (!modCur) {
                    if (Utils::exists(name)) {
                        Utils::restrictedDelete(name);
                    }
                    Utils::writeContents(".gitlite/staging/" + name, "DELETE");
                    handled = true;
                }
            } else if (!inCurrent && inGiven) {
                if (!modGiv) {
                    // File removed in current, unchanged in given -> keep deletion
                    if (Utils::exists(name)) {
                        Utils::restrictedDelete(name);
                    }
                    handled = true;
                }
            } else {
                handled = true; // deleted in both
            }
        } else {
            if (!inCurrent && inGiven) {
                stageBlobFromGiven(givBlob);
                handled = true;
            } else if (inCurrent && !inGiven) {
                handled = true; // only current has it
            } else if (inCurrent && inGiven && curBlob == givBlob) {
                handled = true; // identical add
            }
        }

        if (handled) {
            continue;
        }

        hasConflicts = true;
        std::string curContent = inCurrent ? Utils::readContentsAsString(".gitlite/objects/" + curBlob) : "";
        std::string givContent = inGiven ? Utils::readContentsAsString(".gitlite/objects/" + givBlob) : "";
        std::string conflict = "<<<<<<< HEAD\r\n" + curContent + "=======\r\n" + givContent + ">>>>>>>\r\n";
        std::string blobId = ensureBlob(conflict);
        Utils::writeContents(name, conflict);
        Utils::writeContents(".gitlite/staging/" + name, blobId);
    }

    if (hasConflicts) {
        std::cout << "Encountered a merge conflict." << std::endl;
    }

    auto stagedFiles = Utils::plainFilenamesIn(".gitlite/staging");
    if (stagedFiles.empty()) {
        Utils::exitWithMessage("No changes added to the commit.");
    }

    auto mergedFiles = currentCommitFiles;
    for (const auto &file : stagedFiles) {
        std::string marker = Utils::readContentsAsString(".gitlite/staging/" + file);
        if (marker == "DELETE") {
            mergedFiles.erase(file);
        } else {
            mergedFiles[file] = marker;
        }
    }

    auto now = std::time(nullptr);
    std::string timestamp = std::to_string(now);
    std::string commitContent = "parent " + currentCommitId + " " + givenCommitId + "\n";
    commitContent += "timestamp " + timestamp + "\n";
    commitContent += "message Merged " + branchName + " into " + currentBranch + ".\n";
    commitContent += "files ";
    for (const auto &p : mergedFiles) {
        commitContent += p.first + ":" + p.second + ";";
    }
    commitContent += "\n";

    std::string newCommitId = Utils::sha1(commitContent);
    Utils::writeContents(".gitlite/objects/" + newCommitId, commitContent);
    Utils::writeContents(".gitlite/refs/heads/" + currentBranch, newCommitId);

    if (Utils::isDirectory(".gitlite/staging")) {
        std::string cmd = "rm -rf .gitlite/staging";
        system(cmd.c_str());
    }
}

std::string SomeObj::findSplitPoint(const std::string &commitId1, const std::string &commitId2) {
    auto parseParents = [](const std::string &commitId) {
        std::vector<std::string> parents;
        std::string path = ".gitlite/objects/" + commitId;
        if (!Utils::exists(path)) return parents;
        std::string content = Utils::readContentsAsString(path);
        size_t pos = content.find("parent ");
        if (pos == std::string::npos) return parents;
        size_t end = content.find('\n', pos);
        std::string parentLine = content.substr(pos + 7, end - pos - 7);
        std::istringstream iss(parentLine);
        std::string pid;
        while (iss >> pid) parents.push_back(pid);
        return parents;
    };

    std::unordered_map<std::string, int> dist1;
    std::queue<std::string> q;
    q.push(commitId1);
    dist1[commitId1] = 0;
    while (!q.empty()) {
        std::string cur = q.front();
        q.pop();
        for (const auto &p : parseParents(cur)) {
            if (!dist1.count(p)) {
                dist1[p] = dist1[cur] + 1;
                q.push(p);
            }
        }
    }

    std::string best;
    int bestDist = INT_MAX;
    std::queue<std::pair<std::string, int>> q2;
    q2.push({commitId2, 0});
    std::set<std::string> visited;
    while (!q2.empty()) {
        auto [cur, d] = q2.front();
        q2.pop();
        if (visited.count(cur)) continue;
        visited.insert(cur);
        if (dist1.count(cur) && dist1[cur] + d < bestDist) {
            best = cur;
            bestDist = dist1[cur] + d;
        }
        for (const auto &p : parseParents(cur)) {
            q2.push({p, d + 1});
        }
    }

    return best.empty() ? commitId1 : best;
}

// Adds a new remote repository
void SomeObj::addRemote(const std::string &remoteName, const std::string &remoteDir) {
    std::string remotesDir = ".gitlite/remotes";
    Utils::createDirectories(remotesDir);

    std::string remoteFile = remotesDir + "/" + remoteName;
    if (Utils::exists(remoteFile)) {
        Utils::exitWithMessage("A remote with that name already exists.");
    }

    Utils::writeContents(remoteFile, remoteDir);
}

// Removes a remote repository
void SomeObj::rmRemote(const std::string &remoteName) {
    std::string remoteFile = ".gitlite/remotes/" + remoteName;
    if (!Utils::exists(remoteFile)) {
        Utils::exitWithMessage("A remote with that name does not exist.");
    }

    Utils::restrictedDelete(remoteFile);
}

// Pushes changes to the remote repository
void SomeObj::push(const std::string &remoteName, const std::string &remoteBranchName) {
    std::string remoteFile = ".gitlite/remotes/" + remoteName;
    if (!Utils::exists(remoteFile)) {
        Utils::exitWithMessage("A remote with that name does not exist.");
    }

    std::string remotePath = Utils::readContentsAsString(remoteFile);
    if (!remotePath.empty() && remotePath.back() == '\n') {
        remotePath.pop_back();
    }

    if (!Utils::isDirectory(remotePath)) {
        Utils::exitWithMessage("Remote directory not found.");
    }

    // Get current branch head
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);

    // Check if remote branch exists
    std::string remoteBranchFile = remotePath + "/refs/heads/" + remoteBranchName;
    
    if (Utils::exists(remoteBranchFile)) {
        std::string remoteHeadCommitId = Utils::readContentsAsString(remoteBranchFile);
        
        // Check if remoteHead is in history of currentCommitId
        bool isAncestor = false;
        std::queue<std::string> q;
        q.push(currentCommitId);
        std::set<std::string> visited;
        
        while(!q.empty()) {
            std::string cid = q.front();
            q.pop();
            if (cid == remoteHeadCommitId) {
                isAncestor = true;
                break;
            }
            if (visited.count(cid)) continue;
            visited.insert(cid);
            
            // Get parents
            std::string path = ".gitlite/objects/" + cid;
            if (Utils::exists(path)) {
                std::string content = Utils::readContentsAsString(path);
                size_t pos = content.find("parent ");
                if (pos != std::string::npos) {
                    size_t end = content.find('\n', pos);
                    std::string parentLine = content.substr(pos + 7, end - pos - 7);
                    std::istringstream iss(parentLine);
                    std::string p;
                    while(iss >> p) q.push(p);
                }
            }
        }
        
        if (!isAncestor) {
            Utils::exitWithMessage("Please pull down remote changes before pushing.");
        }
    }

    // Copy objects to remote
    std::queue<std::string> q;
    q.push(currentCommitId);
    std::set<std::string> visited;
    
    while(!q.empty()) {
        std::string commitId = q.front();
        q.pop();
        
        if (visited.count(commitId)) continue;
        visited.insert(commitId);
        
        std::string localObjectPath = ".gitlite/objects/" + commitId;
        std::string remoteObjectPath = remotePath + "/objects/" + commitId;
        
        if (Utils::exists(remoteObjectPath)) {
            continue; 
        }
        
        if (Utils::exists(localObjectPath)) {
            std::string content = Utils::readContentsAsString(localObjectPath);
            Utils::writeContents(remoteObjectPath, content);
            
            // Parse parents
            size_t pos = content.find("parent ");
            if (pos != std::string::npos) {
                size_t end = content.find('\n', pos);
                std::string parentLine = content.substr(pos + 7, end - pos - 7);
                std::istringstream iss(parentLine);
                std::string p;
                while(iss >> p) q.push(p);
            }
            
            // Copy blobs
            size_t filesPos = content.find("files ");
            if (filesPos != std::string::npos) {
                std::string filesSection = content.substr(filesPos + 6);
                size_t start = 0;
                while (start < filesSection.length()) {
                    size_t colonPos = filesSection.find(':', start);
                    if (colonPos == std::string::npos) break;
                    size_t semicolonPos = filesSection.find(';', colonPos);
                    if (semicolonPos == std::string::npos) break;
                    
                    std::string blobId = filesSection.substr(colonPos + 1, semicolonPos - colonPos - 1);
                    std::string localBlobPath = ".gitlite/objects/" + blobId;
                    std::string remoteBlobPath = remotePath + "/objects/" + blobId;
                    
                    if (Utils::exists(localBlobPath) && !Utils::exists(remoteBlobPath)) {
                        std::string blobContent = Utils::readContentsAsString(localBlobPath);
                        Utils::writeContents(remoteBlobPath, blobContent);
                    }
                    start = semicolonPos + 1;
                }
            }
        }
    }
    
    // Update remote branch head
    Utils::writeContents(remoteBranchFile, currentCommitId);
}

// Fetches changes from the remote repository
void SomeObj::fetch(const std::string &remoteName, const std::string &remoteBranchName) {
    std::string remoteFile = ".gitlite/remotes/" + remoteName;
    if (!Utils::exists(remoteFile)) {
        Utils::exitWithMessage("A remote with that name does not exist.");
    }

    std::string remotePath = Utils::readContentsAsString(remoteFile);
    if (!remotePath.empty() && remotePath.back() == '\n') {
        remotePath.pop_back();
    }

    if (!Utils::isDirectory(remotePath)) {
        Utils::exitWithMessage("Remote directory not found.");
    }

    std::string remoteBranchFile = remotePath + "/refs/heads/" + remoteBranchName;
    if (!Utils::exists(remoteBranchFile)) {
        Utils::exitWithMessage("That remote does not have that branch.");
    }

    std::string remoteHeadCommitId = Utils::readContentsAsString(remoteBranchFile);
    
    // Copy objects from remote
    std::queue<std::string> q;
    q.push(remoteHeadCommitId);
    std::set<std::string> visited;

    while(!q.empty()) {
        std::string commitId = q.front();
        q.pop();
        
        if (visited.count(commitId)) continue;
        visited.insert(commitId);

        std::string remoteObjectPath = remotePath + "/objects/" + commitId;
        std::string localObjectPath = ".gitlite/objects/" + commitId;
        
        if (!Utils::exists(remoteObjectPath)) {
            continue; 
        }

        std::string content = Utils::readContentsAsString(remoteObjectPath);
        if (!Utils::exists(localObjectPath)) {
            Utils::writeContents(localObjectPath, content);
        }

        // Parse parents
        size_t pos = content.find("parent ");
        if (pos != std::string::npos) {
            size_t end = content.find('\n', pos);
            std::string parentLine = content.substr(pos + 7, end - pos - 7);
            std::istringstream iss(parentLine);
            std::string p;
            while(iss >> p) {
                q.push(p);
            }
        }

        // Copy blobs
        size_t filesPos = content.find("files ");
        if (filesPos != std::string::npos) {
            std::string filesSection = content.substr(filesPos + 6);
            size_t start = 0;
            while (start < filesSection.length()) {
                size_t colonPos = filesSection.find(':', start);
                if (colonPos == std::string::npos) break;
                size_t semicolonPos = filesSection.find(';', colonPos);
                if (semicolonPos == std::string::npos) break;
                
                std::string blobId = filesSection.substr(colonPos + 1, semicolonPos - colonPos - 1);
                
                std::string remoteBlobPath = remotePath + "/objects/" + blobId;
                std::string localBlobPath = ".gitlite/objects/" + blobId;
                
                if (Utils::exists(remoteBlobPath) && !Utils::exists(localBlobPath)) {
                    std::string blobContent = Utils::readContentsAsString(remoteBlobPath);
                    Utils::writeContents(localBlobPath, blobContent);
                }
                
                start = semicolonPos + 1;
            }
        }
    }

    // Update refs/remotes/<remoteName>/<remoteBranchName>
    std::string refPath = ".gitlite/refs/heads/" + remoteName + "/" + remoteBranchName;
    Utils::writeContents(refPath, remoteHeadCommitId);
}

// Pulls changes from the remote repository
void SomeObj::pull(const std::string &remoteName, const std::string &remoteBranchName) {
    fetch(remoteName, remoteBranchName);
    merge(remoteName + "/" + remoteBranchName);
}


// Helper methods
std::map<std::string, std::string> SomeObj::getFilesInCommit(const std::string &commitId) {
    std::map<std::string, std::string> files;

    std::string commitPath = ".gitlite/objects/" + commitId;
    if (!Utils::exists(commitPath)) {
        return files;
    }

    std::string commitContent = Utils::readContentsAsString(commitPath);
    size_t filesPos = commitContent.find("files ");
    if (filesPos == std::string::npos) {
        return files;
    }

    std::string filesSection = commitContent.substr(filesPos + 6);
    size_t newlinePos = filesSection.find('\n');
    if (newlinePos != std::string::npos) {
        filesSection = filesSection.substr(0, newlinePos);
    }

    // Parse files section
    size_t start = 0;
    while (start < filesSection.length()) {
        size_t colonPos = filesSection.find(':', start);
        if (colonPos == std::string::npos)
            break;

        size_t semicolonPos = filesSection.find(';', colonPos);
        if (semicolonPos == std::string::npos)
            break;

        std::string filename = filesSection.substr(start, colonPos - start);
        std::string blobId = filesSection.substr(colonPos + 1, semicolonPos - colonPos - 1);

        // Only add file if it's not marked as DELETE
        if (blobId != "DELETE") {
            files[filename] = blobId;
        }
        start = semicolonPos + 1;
    }

    return files;
}

bool SomeObj::isFileTrackedInCommit(const std::string &filename, const std::string &commitId) {
    auto files = getFilesInCommit(commitId);
    return files.find(filename) != files.end();
}