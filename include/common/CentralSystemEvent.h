#ifndef INCLUDE_COMMON_CENTRALSYSTEMEVENT_H_
#define INCLUDE_COMMON_CENTRALSYSTEMEVENT_H_

#include <string>
#include <Poco/JSON/Object.h>

struct CentralSystemEvent {
    enum class Type {
    	StatusNotification,
    	StartTransaction,
    	StopTransaction,
    	MeterValues,
    	CallResult,
    	Authorize,
		Heartbeat
    };

    Type type;
    int connectorId = 0;
    std::string status;
    std::string errorCode;
    std::string idTag;
    int meterValue = 0;
    int transactionId = -1;
    std::string reason;
    std::string uniqueId;
    Poco::JSON::Object payload;
};



#endif /* INCLUDE_COMMON_CENTRALSYSTEMEVENT_H_ */
