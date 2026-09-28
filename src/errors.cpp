#include "cyberpower/errors.hpp"

namespace cyberpower {

const char* error_name(Error code) {
  switch (code) {
    case Error::Ok: return "UPS_ERR_SUCCESS";
    case Error::UpsGeneralFault: return "UPS_ERR_GENERAL_FAULT";
    case Error::UpsGeneralFault2: return "UPS_ERR_GENERAL_FAULT2";
    case Error::UpsNotSupport: return "UPS_ERR_NOT_SUPPORT";
    case Error::UpsWrongParameter: return "UPS_ERR_WRONG_PARAMETER";
    case Error::UpsGeneralFault9: return "UPS_ERR_GENERAL_FAULT9";
    case Error::UpsAcceptNotNow: return "UPS_ERR_ACCEPT_NOT_NOW";
    case Error::UpsOutOfRange: return "UPS_ERR_OUT_OF_RANGE";
    case Error::UpsNotExact: return "UPS_ERR_NOT_EXACT";
    case Error::UpsNotAvailable: return "UPS_ERR_NOT_AVAILABLE";
    case Error::UpsOutletCtlFail: return "UPS_ERR_OUTLET_CTL_FAIL";
    case Error::UpsOutletNotSwitch: return "UPS_ERR_OUTLET_NOT_SWITCHe";
    case Error::UpsOutletNotScheduled: return "UPS_ERR_OUTLET_NOT_SCH";
    case Error::UpsMemFault: return "UPS_ERR_MEM_FAULT";
    case Error::UpsMemReadOnly: return "UPS_ERR_MEM_READY_ONLY";
    case Error::UpsMemAddrInvalid: return "UPS_ERR_MEM_ADDR_INVALID";
    case Error::UpsMemDenied: return "UPS_ERR_MEM_DENIED";
    case Error::ReqParamOutOfRange: return "REQ_ERR_PARAM_OUT_OF_RANGE";
    case Error::ReqDataOutOfRange: return "REQ_ERR_DATA_OUT_OF_RANGE";
    case Error::ReqIndexOutOfRange: return "REQ_ERR_INDEX_OUT_OF_RANGE";
    case Error::RespEmpty: return "RESP_ERR_EMPTY";
    case Error::RespFormatEssential: return "RESP_ERR_FORMAT_ESSENTIAL";
    case Error::RespNotNumber: return "RESP_ERR_NOT_NUMBER";
    case Error::RespNotExactNumber: return "RESP_ERR_NOT_EXACT_NUMBER";
    case Error::RespNotNegativeInteger: return "RESP_ERR_NOT_NEGATIVE_INTEGER";
    case Error::RespNotPositiveInteger: return "RESP_ERR_NOT_POSITIVE_INTEGER";
    case Error::RespNotDecimal: return "RESP_ERR_NOT_DECIMAL";
    case Error::RespNotExactDecimal: return "RESP_ERR_NOT_EXACT_DECIMAL";
    case Error::RespNotPositiveDecimal: return "RESP_ERR_NOT_POSITIVE_DECIMAL";
    case Error::RespNoFlagByte: return "RESP_ERR_NO_FLAG_BYTE";
    case Error::RespFlagByteIrregular: return "RESP_ERR_FLAG_BYTE_IRREGULAR";
    case Error::RespNoAvailableItem: return "RESP_ERR_NO_AVAILABLE_ITEM";
    case Error::RespTooLessItems: return "RESP_ERR_TOO_LESS_ITEMS";
    case Error::RespDataOutOfRange: return "RESP_ERR_DATA_OUT_OF_RANGE";
    case Error::RespIndexOutOfRange: return "RESP_ERR_INDEX_OUT_OF_RANGE";
    case Error::Io: return "IO";
    case Error::Timeout: return "TIMEOUT";
    case Error::NotSupported: return "NOT_SUPPORTED";
    case Error::Protocol: return "PROTOCOL";
  }
  return "UNKNOWN";
}

std::string error_message(Error code) {
  return error_name(code);
}

}  // namespace cyberpower
