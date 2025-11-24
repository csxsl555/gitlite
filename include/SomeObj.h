#ifndef SOMEOBJ_H
#define SOMEOBJ_H

#include <string>
#include <vector>
#include <map>

class SomeObj {
public:
    SomeObj();
    
    // Subtask 1 commands
    void init();
    void add(const std::string& filename);
    void commit(const std::string& message);
    void rm(const std::string& filename);
    
    // Subtask 2 commands
    void log();
    void globalLog();
    void find(const std::string& commitMessage);
    void checkoutFile(const std::string& filename);
    void checkoutFileInCommit(const std::string& commitId, const std::string& filename);
    void checkoutBranch(const std::string& branchName);
    
    // Subtask 3 commands
    void status();
    
    // Subtask 4 commands
    void branch(const std::string& branchName);
    void rmBranch(const std::string& branchName);
    void reset(const std::string& commitId);
    
    // Subtask 5 commands
    void merge(const std::string& branchName);
    
    // Subtask 6 commands (remote operations)
    void addRemote(const std::string& remoteName, const std::string& remoteDir);
    void rmRemote(const std::string& remoteName);
    void push(const std::string& remoteName, const std::string& remoteBranchName);
    void fetch(const std::string& remoteName, const std::string& remoteBranchName);
    void pull(const std::string& remoteName, const std::string& remoteBranchName);

private:
    // Helper methods
    std::map<std::string, std::string> getFilesInCommit(const std::string& commitId);
    bool isFileTrackedInCommit(const std::string& filename, const std::string& commitId);
};

#endif // SOMEOBJ_H