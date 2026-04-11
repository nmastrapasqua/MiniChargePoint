#include "firmware/ErrorSimulator.h"
#include "firmware/ConnectorSimulator.h"

ErrorSimulator::ErrorSimulator(ConnectorSimulator& connector)
    : _connector(connector),
      _currentError(ErrorType::None)
{
}

void ErrorSimulator::triggerError(ErrorType type)
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (type == ErrorType::None)
        return;
    _currentError = type;
    _connector.fault();
}

void ErrorSimulator::clearError()
{
    Poco::Mutex::ScopedLock lock(_mutex);
    if (_currentError == ErrorType::None)
        return;
    _currentError = ErrorType::None;
    _connector.clearFault();
}

ErrorSimulator::ErrorType ErrorSimulator::getCurrentError() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    return _currentError;
}

std::string ErrorSimulator::getErrorCodeOcpp() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    switch (_currentError) {
        case ErrorType::HardwareFault:   return "InternalError";
        case ErrorType::TamperDetection: return "OtherError";
        default:                         return "NoError";
    }
}

std::string ErrorSimulator::getErrorType() const
{
    Poco::Mutex::ScopedLock lock(_mutex);
    switch (_currentError) {
        case ErrorType::HardwareFault:   return "HardwareFault";
        case ErrorType::TamperDetection: return "TamperDetection";
        default:                         return "";
    }
}
