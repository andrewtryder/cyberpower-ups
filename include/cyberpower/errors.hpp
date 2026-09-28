#pragma once

#include <cstdint>
#include <string>

namespace cyberpower {

// Numeric values below were recovered from libppbedrvc.dylib.
//
// RESP_* values are High confidence: StatusResponser stores 0xC8 on an empty
// buffer, 0xC9 when the frame is not '#' ... CR, 0xD1 (209) and 0xD3 (211)
// on the flag-byte / no-item paths. Those four land exactly on a sequential
// enum that starts at 200 and follows the RESP_ERR_* cstring table.
//
// UPS_ERR_* and REQ_ERR_* follow the same cstring table order. Their numeric
// bases are Medium: the names and relative order are in the binary, but the
// absolute starting value 0 was not pinned by an immediate the way 200 was.

enum class Error : int {
  Ok = 0,

  // Device-reported command results. Medium numeric values, High names.
  UpsSuccess = 0,             // UPS_ERR_SUCCESS
  UpsGeneralFault = 1,        // UPS_ERR_GENERAL_FAULT          "General fault(1)"
  UpsGeneralFault2 = 2,       // UPS_ERR_GENERAL_FAULT2         "General fault(2)"
  UpsNotSupport = 3,          // UPS_ERR_NOT_SUPPORT
  UpsWrongParameter = 4,      // UPS_ERR_WRONG_PARAMETER
  UpsGeneralFault9 = 5,       // UPS_ERR_GENERAL_FAULT9         "General fault(9)"
  UpsAcceptNotNow = 6,        // UPS_ERR_ACCEPT_NOT_NOW
  UpsOutOfRange = 7,          // UPS_ERR_OUT_OF_RANGE
  UpsNotExact = 8,            // UPS_ERR_NOT_EXACT
  UpsNotAvailable = 9,        // UPS_ERR_NOT_AVAILABLE
  UpsOutletCtlFail = 10,      // UPS_ERR_OUTLET_CTL_FAIL
  UpsOutletNotSwitch = 11,    // UPS_ERR_OUTLET_NOT_SWITCHe  (spelling is the binary's)
  UpsOutletNotScheduled = 12, // UPS_ERR_OUTLET_NOT_SCH
  UpsMemFault = 13,           // UPS_ERR_MEM_FAULT
  UpsMemReadOnly = 14,        // UPS_ERR_MEM_READY_ONLY      (spelling is the binary's)
  UpsMemAddrInvalid = 15,     // UPS_ERR_MEM_ADDR_INVALID
  UpsMemDenied = 16,          // UPS_ERR_MEM_DENIED

  ReqParamOutOfRange = 17,    // REQ_ERR_PARAM_OUT_OF_RANGE   Medium
  ReqDataOutOfRange = 18,     // REQ_ERR_DATA_OUT_OF_RANGE    Medium
  ReqIndexOutOfRange = 19,    // REQ_ERR_INDEX_OUT_OF_RANGE   Medium

  // Parser results. High: base 200 confirmed by immediates 0xC8, 0xC9, 0xD1, 0xD3.
  RespEmpty = 200,                 // RESP_ERR_EMPTY
  RespFormatEssential = 201,       // RESP_ERR_FORMAT_ESSENTIAL
  RespNotNumber = 202,             // RESP_ERR_NOT_NUMBER
  RespNotExactNumber = 203,        // RESP_ERR_NOT_EXACT_NUMBER
  RespNotNegativeInteger = 204,    // RESP_ERR_NOT_NEGATIVE_INTEGER
  RespNotPositiveInteger = 205,    // RESP_ERR_NOT_POSITIVE_INTEGER
  RespNotDecimal = 206,            // RESP_ERR_NOT_DECIMAL
  RespNotExactDecimal = 207,       // RESP_ERR_NOT_EXACT_DECIMAL
  RespNotPositiveDecimal = 208,    // RESP_ERR_NOT_POSITIVE_DECIMAL
  RespNoFlagByte = 209,            // RESP_ERR_NO_FLAG_BYTE
  RespFlagByteIrregular = 210,     // RESP_ERR_FLAG_BYTE_IRREGULAR
  RespNoAvailableItem = 211,       // RESP_ERR_NO_AVAILABLE_ITEM
  RespTooLessItems = 212,          // RESP_ERR_TOO_LESS_ITEMS
  RespDataOutOfRange = 213,        // RESP_ERR_DATA_OUT_OF_RANGE
  RespIndexOutOfRange = 214,       // RESP_ERR_INDEX_OUT_OF_RANGE

  // Local transport failures. These are this library, not the driver.
  Io = 1000,
  Timeout = 1001,
  NotSupported = 1002,
  Protocol = 1003,
};

// v3 binary packet status stored at Request+0x20. High: each immediate in
// SendRequest / VerifyPacket / CheckHandShake matches this cstring order:
// SUCCESS, RESONSE_EMPTY (spelling is the binary's), NO_HANDSHAKE,
// CHECKSUM_FAIL, PAYLOAD_FORMAT_FAIL, WRITE_FAIL, RESPONSE_FAIL, HANDSHAKE_FAIL.
enum class V3Status : int {
  Success = 0,
  ResponseEmpty = 1,
  NoHandshake = 2,
  ChecksumFail = 3,
  PayloadFormatFail = 4,
  WriteFail = 5,
  ResponseFail = 6,
  HandshakeFail = 7,
};

const char* error_name(Error code);
std::string error_message(Error code);

}  // namespace cyberpower
