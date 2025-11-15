//
// Created by 余泓 on 2025/11/15.
//

#include "ClientInfo.h"

ClientInfo *ClientInfo::instance() {
    static ClientInfo instance;
    return &instance;
}

bool ClientInfo::isCaller() {
    return isCaller_;
}

void ClientInfo::setIsCaller(bool isCaller) { isCaller_ = isCaller; }

void ClientInfo::setLocalId(const std::string &id) {
    localId_ = id;
}

std::string ClientInfo::getLocalId() const { return localId_; }

void ClientInfo::setRemoteId(const std::string &id) {
    remoteId_ = id;
}

std::string ClientInfo::getRemoteId() const {
    return remoteId_;
}
