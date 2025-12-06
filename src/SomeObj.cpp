#include "../include/SomeObj.h"
#include "../include/Repository.h"
#include "../include/Utils.h"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>

SomeObj::SomeObj() {}

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
        std::cout << "Date: " << timestamp << std::endl;
        std::cout << message << std::endl
                  << std::endl;

        currentCommitId = parent;
    }
}

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

    // === Modifications Not Staged For Commit === (Bonus - leave empty for now)
    std::cout << std::endl
              << "=== Modifications Not Staged For Commit ===" << std::endl;

    // === Untracked Files === (Bonus - implement basic version)
    std::cout << std::endl
              << "=== Untracked Files ===" << std::endl;
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

void SomeObj::merge(const std::string &branchName) {
    // Error checks
    if (branchName.empty()) {
        Utils::exitWithMessage("Incorrect operands.");
    }
    
    // Get current branch
    std::string headContent = Utils::readContentsAsString(".gitlite/HEAD");
    std::string currentBranch = headContent.substr(16);
    
    if (currentBranch == branchName) {
        Utils::exitWithMessage("Cannot merge a branch with itself.");
    }
    
    // Check if branch exists
    std::string branchPath = ".gitlite/refs/heads/" + branchName;
    if (!Utils::exists(branchPath)) {
        Utils::exitWithMessage("A branch with that name does not exist.");
    }
    
    // Check for uncommitted changes
    if (Utils::isDirectory(".gitlite/staging") && !Utils::plainFilenamesIn(".gitlite/staging").empty()) {
        Utils::exitWithMessage("You have uncommitted changes.");
    }
    
    // Get commit IDs
    std::string currentCommitId = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);
    std::string givenCommitId = Utils::readContentsAsString(branchPath);
    
    // Check for untracked files that would be overwritten
    auto givenCommitFiles = getFilesInCommit(givenCommitId);
    auto workingFiles = Utils::plainFilenamesIn(".");
    auto currentCommitFiles = getFilesInCommit(currentCommitId);
    
    for (const auto &file : workingFiles) {
        if (file == ".gitlite" || file.substr(0, 9) == ".gitlite/") {
            continue;
        }
        
        if (givenCommitFiles.find(file) != givenCommitFiles.end() && 
            currentCommitFiles.find(file) == currentCommitFiles.end()) {
            Utils::exitWithMessage("There is an untracked file in the way; delete it, or add and commit it first.");
        }
    }
    
    // Find split point
    std::string splitPointId = findSplitPoint(currentCommitId, givenCommitId);
    
    // Check for special cases
    if (splitPointId == givenCommitId) {
        std::cout << "Given branch is an ancestor of current branch." << std::endl;
        return;
    }
    
    if (splitPointId == currentCommitId) {
        // Fast-forward case
        std::cout << "Current branch fast-forwarded." << std::endl;
        
        // Update branch pointer
        Utils::writeContents(".gitlite/refs/heads/" + currentBranch, givenCommitId);
        
        // Update working directory to match given branch
        auto targetFiles = getFilesInCommit(givenCommitId);
        for (const auto &pair : targetFiles) {
            std::string blobPath = ".gitlite/objects/" + pair.second;
            std::string content = Utils::readContentsAsString(blobPath);
            Utils::writeContents(pair.first, content);
        }
        
        // Delete files that are in current commit but not in given commit
        for (const auto &pair : currentCommitFiles) {
            if (targetFiles.find(pair.first) == targetFiles.end()) {
                Utils::restrictedDelete(pair.first);
            }
        }
        
        return;
    }
    
    // Get file states at split point, current branch, and given branch
    auto splitPointFiles = getFilesInCommit(splitPointId);
    auto currentFiles = getFilesInCommit(currentCommitId);
    auto givenFiles = getFilesInCommit(givenCommitId);
    
    bool hasConflicts = false;
    Utils::createDirectories(".gitlite/staging");
    
    // Process all files that appear in any of the three commits
    std::set<std::string> allFiles;
    for (const auto &pair : splitPointFiles) allFiles.insert(pair.first);
    for (const auto &pair : currentFiles) allFiles.insert(pair.first);
    for (const auto &pair : givenFiles) allFiles.insert(pair.first);
    
    for (const auto &file : allFiles) {
        bool inSplit = splitPointFiles.find(file) != splitPointFiles.end();
        bool inCurrent = currentFiles.find(file) != currentFiles.end();
        bool inGiven = givenFiles.find(file) != givenFiles.end();
        
        std::string splitContent = inSplit ? Utils::readContentsAsString(".gitlite/objects/" + splitPointFiles[file]) : "";
        std::string currentContent = inCurrent ? Utils::readContentsAsString(".gitlite/objects/" + currentFiles[file]) : "";
        std::string givenContent = inGiven ? Utils::readContentsAsString(".gitlite/objects/" + givenFiles[file]) : "";
        
        // Case 1: Modified in given branch, unchanged in current branch
        if (inCurrent && inGiven && splitContent == currentContent && currentContent != givenContent) {
            // Update to given version and stage
            Utils::writeContents(file, givenContent);
            Utils::writeContents(".gitlite/staging/" + file, givenFiles[file]);
        }
        // Case 2: Modified in current branch, unchanged in given branch
        else if (inCurrent && inGiven && splitContent == givenContent && splitContent != currentContent) {
            // Keep current version (no action needed)
        }
        // Case 3: Modified in same way in both branches
        else if (inCurrent && inGiven && splitContent != currentContent && currentContent == givenContent) {
            // Keep current version (no action needed)
        }
        // Case 4: Deleted in both branches but exists in working directory
        else if (inSplit && !inCurrent && !inGiven && Utils::exists(file)) {
            // Keep file as untracked (no action needed)
        }
        // Case 5: Unmodified in current, exists only in given branch
        else if (!inCurrent && inGiven && !inSplit) {
            // Checkout from given branch and stage
            Utils::writeContents(file, givenContent);
            Utils::writeContents(".gitlite/staging/" + file, givenFiles[file]);
        }
        // Case 6: Exists in split point, unmodified in given, not tracked in current
        else if (!inCurrent && inGiven && inSplit && splitContent == givenContent) {
            // Keep as is (should not be staged)
        }
        // Case 7: Exists in split point, unmodified in current, not tracked in given
        else if (inCurrent && !inGiven && inSplit && splitContent == currentContent) {
            // Stage for removal
            Utils::writeContents(".gitlite/staging/" + file, "DELETE");
            if (Utils::exists(file)) {
                Utils::restrictedDelete(file);
            }
        }
        // Case 8: Conflicts
        else if ((inCurrent && inGiven && 
                  ((splitContent != currentContent && splitContent != givenContent && currentContent != givenContent) ||
                   (inSplit && splitContent == currentContent && splitContent != givenContent) ||
                   (inSplit && splitContent == givenContent && splitContent != currentContent) ||
                   (!inSplit && currentContent != givenContent)))) {
            // Create conflict
            hasConflicts = true;
            std::string conflictContent = "<<<<<<< HEAD\n" + currentContent + "=======\n" + givenContent + ">>>>>>>\n";
            Utils::writeContents(file, conflictContent);
            Utils::writeContents(".gitlite/staging/" + file, Utils::sha1(conflictContent));
        }
    }
    
    if (hasConflicts) {
        std::cout << "Encountered a merge conflict." << std::endl;
    } else {
        // Auto-commit the merge
        std::string mergeMessage = "Merged " + branchName + " into " + currentBranch;
        commit(mergeMessage);
        
        // Update the commit to have two parents (for merge commits)
        std::string newHeadContent = Utils::readContentsAsString(".gitlite/refs/heads/" + currentBranch);
        std::string commitPath = ".gitlite/objects/" + newHeadContent;
        std::string commitContent = Utils::readContentsAsString(commitPath);
        
        // Replace parent line with two parents
        size_t parentPos = commitContent.find("parent ");
        if (parentPos != std::string::npos) {
            size_t parentEnd = commitContent.find('\n', parentPos);
            std::string newParentLine = "parent " + currentCommitId + " " + givenCommitId;
            commitContent.replace(parentPos, parentEnd - parentPos, newParentLine);
        }
        
        // Recreate commit with two parents
        Utils::writeContents(commitPath, commitContent);
    }
}

std::string SomeObj::findSplitPoint(const std::string &commitId1, const std::string &commitId2) {
    // Get all ancestors of commit1
    std::set<std::string> ancestors1;
    std::string current = commitId1;
    while (!current.empty()) {
        ancestors1.insert(current);
        std::string commitPath = ".gitlite/objects/" + current;
        if (!Utils::exists(commitPath)) break;
        
        std::string commitContent = Utils::readContentsAsString(commitPath);
        size_t pos = commitContent.find("parent ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            current = commitContent.substr(pos + 7, end - pos - 7);
        } else {
            break;
        }
    }
    
    // Walk back from commit2 until we find an ancestor of commit1
    current = commitId2;
    while (!current.empty()) {
        if (ancestors1.find(current) != ancestors1.end()) {
            return current;
        }
        
        std::string commitPath = ".gitlite/objects/" + current;
        if (!Utils::exists(commitPath)) break;
        
        std::string commitContent = Utils::readContentsAsString(commitPath);
        size_t pos = commitContent.find("parent ");
        if (pos != std::string::npos) {
            size_t end = commitContent.find('\n', pos);
            current = commitContent.substr(pos + 7, end - pos - 7);
        } else {
            break;
        }
    }
    
    return "";
}

void SomeObj::addRemote(const std::string &remoteName, const std::string &remoteDir) {
    std::string remotesDir = ".gitlite/remotes";
    Utils::createDirectories(remotesDir);

    std::string remoteFile = remotesDir + "/" + remoteName;
    if (Utils::exists(remoteFile)) {
        Utils::exitWithMessage("A remote with that name already exists.");
    }

    Utils::writeContents(remoteFile, remoteDir);
}

void SomeObj::rmRemote(const std::string &remoteName) {
    std::string remoteFile = ".gitlite/remotes/" + remoteName;
    if (!Utils::exists(remoteFile)) {
        Utils::exitWithMessage("A remote with that name does not exist.");
    }

    Utils::restrictedDelete(remoteFile);
}

void SomeObj::push(const std::string &remoteName, const std::string &remoteBranchName) {
    // TODO: Implement push command
    Utils::message("push command not yet implemented.");
}

void SomeObj::fetch(const std::string &remoteName, const std::string &remoteBranchName) {
    // TODO: Implement fetch command
    Utils::message("fetch command not yet implemented.");
}

void SomeObj::pull(const std::string &remoteName, const std::string &remoteBranchName) {
    // TODO: Implement pull command
    Utils::message("pull command not yet implemented.");
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

        files[filename] = blobId;
        start = semicolonPos + 1;
    }

    return files;
}

bool SomeObj::isFileTrackedInCommit(const std::string &filename, const std::string &commitId) {
    auto files = getFilesInCommit(commitId);
    return files.find(filename) != files.end();
}