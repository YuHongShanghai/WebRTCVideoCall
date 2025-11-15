//
// Created by 余泓 on 2025/11/15.
//

#ifndef CLIENTINFO_H
#define CLIENTINFO_H
#include <string>


class ClientInfo {

public:
    static ClientInfo *instance();
    bool isCaller();
    void setIsCaller(bool isCaller);
    void setLocalId(const std::string& id);
    std::string getLocalId() const;
    void setRemoteId(const std::string& id);
    std::string getRemoteId() const;

private:
    ClientInfo() = default;

    bool isCaller_ = false;
    std::string localId_;
    std::string remoteId_;
};

#endif //CLIENTINFO_H
