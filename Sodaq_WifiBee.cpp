/* The MIT License (MIT)

 Copyright (c) <2015> <Gabriel Notman & M2M4ALL BV>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE. */

#include "Sodaq_WifiBee.h"

#define ENABLE_RADIO_DIAG 1

#if ENABLE_RADIO_DIAG
#define diagPrint(...) { if (_diagStream) _diagStream->print(__VA_ARGS__); }
#define diagPrintLn(...) { if (_diagStream) _diagStream->println(__VA_ARGS__); }
#else
#define diagPrint(...)
#define diagPrintLn(...)
#endif

// Lua prompts
#define LUA_PROMPT "\r\n> "
#define CONNECT_PROMPT "|C|"
#define RECONNECT_PROMPT "|RC|"
#define DISCONNECT_PROMPT "|DC|"
#define SENT_PROMPT "|DS|"
#define RECEIVED_PROMPT "|DR|"
#define STATUS_PROMPT "|STS|"
#define SOF_PROMPT "|SOF|"
#define EOF_PROMPT "|EOF|"

// Lua connection callback scripts
#define CONNECT_CALLBACK "function(s) print(\"|C|\") end"
#define RECONNECT_CALLBACK "function(s) print(\"|RC|\") end"
#define DISCONNECT_CALLBACK "function(s) print(\"|DC|\") end"
#define SENT_CALLBACK "function(s) print(\"|DS|\") end"
#define RECEIVED_CALLBACK "function(s, d) lastData=d print(\"|DR|\") end"
#define STATUS_CALLBACK "print(\"|\" .. \"STS|\" .. wifi.sta.status() .. \"|\")"
#define READ_BACK "uart.write(0, \"|\" .. \"SOF|\" .. lastData .. \"|EOF|\")"

// Timeout constants
#define RESPONSE_TIMEOUT 2000
#define WIFI_CONNECT_TIMEOUT 4000
#define SERVER_CONNECT_TIMEOUT 5000
#define SERVER_RESPONSE_TIMEOUT 5000
#define SERVER_DISCONNECT_TIMEOUT 2000
#define READBACK_TIMEOUT 2500
#define WAKE_DELAY 1000
#define STATUS_DELAY 1000

/*! 
* Initialises
* \li `_APN`, `_username` and `_password` to "". \n
* \li `_bufferSize` and `_bufferUsed` to 0. \n
* \li `_buffer`, `_dataStream` and `_diagStream` to NULL. \n
* \li `_dtrPin` to 0xFF.
*/
Sodaq_WifiBee::Sodaq_WifiBee()
{
  _APN = "";
  _username = "";
  _password = "";

  _bufferSize = 0;
  _bufferUsed = 0;
  _buffer = NULL;

  _dataStream = NULL;
  _diagStream = NULL;

  // Initialize to some unlikely value
  _dtrPin = 0xFF;               
}

/*! 
* Frees any memory allocated to `_buffer`.
*/
Sodaq_WifiBee::~Sodaq_WifiBee()
{
  if (_buffer) {
    free(_buffer);
  }
}

/*! 
* This method initialises the Sodaq_WifiBee object.\n
* @param stream A reference to the stream object used for communicating with the WifiBee. 
* @param dtrPin The IO pin connected to the Bee socket's DTR pin.
* @param bufferSize The amount of memory to allocate to `_buffer`.
*/
void Sodaq_WifiBee::init(Stream& stream, const uint8_t dtrPin,
  const size_t bufferSize)
{
  _dataStream = &stream;
  _dtrPin = dtrPin;

  _bufferSize = bufferSize;
  if (_buffer) {
    free(_buffer);
  }
  _buffer = (uint8_t*)malloc(_bufferSize);

  pinMode(_dtrPin, OUTPUT);

  off();
}

/*!
* This method sets the credentials for the Wifi network.
* @param APN The wifi network's SSID.
* @param username Unused.
* @param password The password for the wifi network.
*/
void Sodaq_WifiBee::connectionSettings(const char* APN, const char* username,
    const char* password)
{
  _APN = APN;
  _username = username;
  _password = password;
}

/*!
* \overload
*/
void Sodaq_WifiBee::connectionSettings(const String& APN, const String& username,
  const String& password)
{
  connectionSettings(APN.c_str(), username.c_str(), password.c_str());
}

/*! 
* This method sets the reference of the stream object to use for debug/diagnostic purposes.
* @param stream The reference to the stream object.
*/
void Sodaq_WifiBee::setDiag(Stream& stream)
{
  _diagStream = &stream;
}

/*! 
* This method can be used to identify the specific Bee module.
* @return The literal constant "WifiBee".
*/
const char* Sodaq_WifiBee::getDeviceType()
{
  return "WifiBee";
}

/*! 
* This method switches on the WifiBee. \n
* It is called automatically, as required, by most methods.
*/
void Sodaq_WifiBee::on()
{
  diagPrintLn("\r\nPower ON");
  digitalWrite(_dtrPin, LOW);
  skipForTime(WAKE_DELAY);
}

/*! 
* This method switches off the WifiBee. \n
* It is called automatically, as required, by most methods.
*/
void Sodaq_WifiBee::off()
{
  diagPrintLn("\r\nPower OFF");
  digitalWrite(_dtrPin, HIGH);
}

// HTTP methods
/*! 
* This method constructs and sends a generic HTTP request.
* @param server The server/host to connect to (IP address or domain).
* @param port The port to connect to.
* @param The HTTP method to use. e.g. "GET", "POST" etc.
* @param location The resource location on the server/host.
* @param headers Any additional headers, each must be separated by CRLF. Must not end in CRLF. \n
* HOST & Content-Length headers are added automatically.
* @param body The body (can be blank) to send with the request. Must not start with a CRLF.
* @param httpCode The HTTP response code is written to this parameter (if a response is received).
* @return `true` if a connection is established and the data is sent, `false` otherwise.
*/
bool Sodaq_WifiBee::HTTPAction(const char* server, const uint16_t port,
    const char* method, const char* location, const char* headers,
    const char* body, uint16_t& httpCode)
{
  bool result;

  // Open the connection
  result = openConnection(server, port, "net.TCP");

  if (result) {
    print("wifiConn:send(\"");

    print(method);
    print(" ");
    print(location);
    print(" HTTP/1.1\\r\\n");

    print("HOST: ");
    print(server);
    print(":");
    print(port);
    print("\\r\\n");

    print("Content-Length: ");
    print(strlen(body));
    print("\\r\\n");

    sendEscapedAscii(headers);
    print("\\r\\n\\r\\n");

    sendEscapedAscii(body);

    println("\")");
  }

  // Wait till we hear that it was sent
  if (result) {
    result = skipTillPrompt(SENT_PROMPT, RESPONSE_TIMEOUT);
  }

  // Wait till we get the data received prompt
  if (result) {
    if (skipTillPrompt(RECEIVED_PROMPT, SERVER_RESPONSE_TIMEOUT)) {
      readServerResponse();
      parseHTTPResponse(httpCode);
    } else {
      clearBuffer();
    }
  }
  
  // The connection might have closed automatically
  // Or it failed to open
  closeConnection();

  return result;
}

/*!
*\overload
*/
bool Sodaq_WifiBee::HTTPAction(const String& server, const uint16_t port,
  const String& method, const String& location, const String& headers,
  const String& body, uint16_t& httpCode)
{
  return HTTPAction(server.c_str(), port, method.c_str(), location.c_str(),
    headers.c_str(), body.c_str(), httpCode);
}

/*!
* This method constructs and sends a HTTP GET request.
* @param server The server/host to connect to (IP address or domain).
* @param port The port to connect to.
* @param location The resource location on the server/host.
* @param headers Any additional headers, each must be separated by CRLF. Must not end in CRLF. \n
* HOST header is added automatically.
* @param httpCode The HTTP response code is written to this parameter (if a response is received).
* @return `true` if a connection is established and the data is sent, `false` otherwise.
*/
bool Sodaq_WifiBee::HTTPGet(const char* server, const uint16_t port,
    const char* location, const char* headers, uint16_t& httpCode)
{
  bool result;

  // Open the connection
  result = openConnection(server, port, "net.TCP");

  if (result) {
    print("wifiConn:send(\"");

    print("GET ");
    print(location);
    print(" HTTP/1.1\\r\\n");

    print("HOST: ");
    print(server);
    print(":");
    print(port);
    print("\\r\\n");

    sendEscapedAscii(headers);
    print("\\r\\n\\r\\n");

    println("\")");
  }

  // Wait till we hear that it was sent
  if (result) {
    result = skipTillPrompt(SENT_PROMPT, RESPONSE_TIMEOUT);
  }

  // Wait till we get the data received prompt
  if (result) {
    if (skipTillPrompt(RECEIVED_PROMPT, SERVER_RESPONSE_TIMEOUT)) {
      readServerResponse();
      parseHTTPResponse(httpCode);
    } else {
      clearBuffer();
    }
  }

  // The connection might have closed automatically
  // Or it failed to open
  closeConnection();

  return result;
}

/*!
*\overload
*/
bool Sodaq_WifiBee::HTTPGet(const String& server, const uint16_t port,
  const String& location, const String& headers, uint16_t& httpCode)
{
  return HTTPGet(server.c_str(), port, location.c_str(), headers.c_str(), httpCode);
}

/*!
* This method constructs and sends a HTTP POST request.
* @param server The server/host to connect to (IP address or domain).
* @param port The port to connect to.
* @param location The resource location on the server/host.
* @param headers Any additional headers, each must be separated by CRLF. Must not end in CRLF. \n
* HOST & Content-Length headers are added automatically.
* @param body The body (can be blank) to send with the request. Must not start with a CRLF.
* @param httpCode The HTTP response code is written to this parameter (if a response is received).
* @return `true` if a connection is established and the data is sent, `false` otherwise.
*/
bool Sodaq_WifiBee::HTTPPost(const char* server, const uint16_t port,
    const char* location, const char* headers, const char* body,
    uint16_t& httpCode)
{
  bool result;

  // Open the connection
  result = openConnection(server, port, "net.TCP");

  if (result) {
    print("wifiConn:send(\"");

    print("POST ");
    print(location);
    print(" HTTP/1.1\\r\\n");

    print("HOST: ");
    print(server);
    print(":");
    print(port);
    print("\\r\\n");

    print("Content-Length: ");
    print(strlen(body));
    print("\\r\\n");

    sendEscapedAscii(headers);
    print("\\r\\n\\r\\n");

    sendEscapedAscii(body);

    println("\")");
  }

  // Wait till we hear that it was sent
  if (result) {
    result = skipTillPrompt(SENT_PROMPT, RESPONSE_TIMEOUT);
  }

  // Wait till we get the data received prompt
  if (result) {
    if (skipTillPrompt(RECEIVED_PROMPT, SERVER_RESPONSE_TIMEOUT)) {
      readServerResponse();
      parseHTTPResponse(httpCode);
    }
    else {
      clearBuffer();
    }
  }

  // The connection might have closed automatically
  // Or it failed to open
  closeConnection();

  return result;
}

/*!
*\overload
*/
bool Sodaq_WifiBee::HTTPPost(const String& server, const uint16_t port,
  const String& location, const String& headers, const String& body,
  uint16_t& httpCode)
{
  return HTTPPost(server.c_str(), port, location.c_str(), headers.c_str(),
    body.c_str(), httpCode);
}

// TCP methods
/*! 
* This method opens a TCP connection to a remote server.
* @param server The server/host to connect to (IP address or domain).
* @param port The port to connect to.
* @return `true` if the connection was successfully opened, otherwise `false`.
*/
bool Sodaq_WifiBee::openTCP(const char* server, uint16_t port)
{
  return openConnection(server, port, "net.TCP");
}

/*! 
* \overload
*/
bool Sodaq_WifiBee::openTCP(const String& server, uint16_t port)
{
  return openTCP(server.c_str(), port);
}

/*! 
* This method sends an ASCII chunk of data over an open TCP connection.
* @param data The buffer containing the data to be sent.
* @return `true` if the data was successfully sent, otherwise `false`.
*/
bool Sodaq_WifiBee::sendTCPAscii(const char* data)
{
  return transmitAsciiData(data);
}

/*!
* \overload
*/
bool Sodaq_WifiBee::sendTCPAscii(const String& data)
{
  return sendTCPAscii(data.c_str());
}

/*! 
* This method sends a binary chunk of data over an open TCP connection.
* @param data The buffer containing the data to be sent.
* @param length The number of bytes, contained in `data`, to send.
* @return `true` if the data was successfully sent, otherwise `false`.
*/
bool Sodaq_WifiBee::sendTCPBinary(const uint8_t* data, const size_t length)
{
  return transmitBinaryData(data, length);
}

/*! 
* This method closes an open TCP connection.
* @return `true` if the connection was closed, otherwise `false`. \n
* It will return `false` if the connection was already closed.
*/
bool Sodaq_WifiBee::closeTCP()
{
  return closeConnection();
}

// UDP methods
/*!
* This method opens a UDP connection to a remote server.
* @param server The server/host to connect to (IP address or domain).
* @param port The port to connect to.
* @return `true` if the connection was successfully opened, otherwise `false`.
*/
bool Sodaq_WifiBee::openUDP(const char* server, uint16_t port)
{
  return openConnection(server, port, "net.UDP");
}

/*!
* \overload
*/
bool Sodaq_WifiBee::openUDP(const String& server, uint16_t port)
{
  return openUDP(server.c_str(), port);
}

/*!
* This method sends an ASCII chunk of data over an open UDP connection.
* @param data The buffer containing the data to be sent.
* @return `true` if the data was successfully sent, otherwise `false`.
*/
bool Sodaq_WifiBee::sendUDPAscii(const char* data)
{
  return transmitAsciiData(data);
}

/*!
* \overload
*/
bool Sodaq_WifiBee::sendUDPAscii(const String& data)
{
  return sendUDPAscii(data.c_str());
}

/*!
* This method sends a binary chunk of data over an open UDP connection.
* @param data The buffer containing the data to be sent.
* @param length The number of bytes, contained in `data`, to send.
* @return `true` if the data was successfully sent, otherwise `false`.
*/
bool Sodaq_WifiBee::sendUDPBinary(const uint8_t* data, const size_t length)
{
  return transmitBinaryData(data, length);
}

/*!
* This method closes an open UDP connection.
* @return `true` if the connection was closed, otherwise `false`. \n
* It will return `false` if the connection was already closed.
*/
bool Sodaq_WifiBee::closeUDP()
{
  return closeConnection();
}

/*!
* This method copies the response data into a supplied buffer. \n
* The amount of data copied is limited by the size of the supplied buffer. \n
* Adds a terminating '\0'.
* @param buffer The buffer to copy the data into.
* @param size The size of `buffer`.
* @param bytesRead The number of bytes copied is written to this parameter.
* @return `false` if there is no data to copy, otherwise `true`.
*/
bool Sodaq_WifiBee::readResponseAscii(char* buffer, const size_t size, size_t& bytesRead)
{
  if (_bufferUsed == 0) {
    return false;
  }

  bytesRead = min((size - 1), _bufferUsed);

  memcpy(buffer, _buffer, bytesRead);
  buffer[bytesRead] = '\0';

  return true;
}

/*!
* This method copies the response data into a supplied buffer. \n
* The amount of data copied is limited by the size of the supplied buffer. \n
* Does not add a terminating '\0'.
* @param buffer The buffer to copy the data into.
* @param size The size of `buffer`.
* @param bytesRead The number of bytes copied is written to this parameter. \n
* @return `false` if there is no data to copy, otherwise `true`.
*/
bool Sodaq_WifiBee::readResponseBinary(uint8_t* buffer, const size_t size, size_t& bytesRead)
{
  if (_bufferUsed == 0) {
    return false;
  }

  bytesRead = min(size, _bufferUsed);

  memcpy(buffer, _buffer, bytesRead);

  return true;
}

/*!
* This method copies the response data into a supplied buffer. \n
* It skips the response and header lines and only copies the response body. \n
* The amount of data copied is limited by the size of the supplied buffer. \n
* Adds a terminating '\0'.
* @param buffer The buffer to copy the data into.
* @param size The size of `buffer`.
* @param bytesRead The number of bytes copied is written to this parameter.
* @param httpCode The HTTP response code is written to this parameter.
* @return `false` if there is no data to copy, otherwise `true`.
* It will return `false` if the body is empty or cannot be determined.
*/
bool Sodaq_WifiBee::readHTTPResponse(char* buffer, const size_t size,
    size_t& bytesRead, uint16_t& httpCode)
{
  if (_bufferUsed == 0) {
    return false;
  }

  // Read HTTP response code
  parseHTTPResponse(httpCode);
  
  // Add 4 to start from after the double newline
  char* startPos = strstr((char*)_buffer, "\r\n\r\n") + 4;

  size_t startIndex = startPos - (char*)_buffer;
  bytesRead = min((size - 1), (_bufferUsed - startIndex));

  memcpy(buffer, startPos, bytesRead);
  buffer[bytesRead] = '\0';

  return true;
}

// Stream implementations
/*!
* Implementation of Stream::write() \n
* If `_dataStream != NULL` it calls `_dataStream->write(x)`.
* @param x parameter to pass to `_dataStream->write()`.
* @return result of `_dataStream->write(x)` or 0 if `_dataStream == NULL`.
*/
size_t Sodaq_WifiBee::write(uint8_t x)
{ 
  if (_dataStream) {
    return _dataStream->write(x);
  } else {
    return 0;
  }
}

/*!
* Implementation of Stream::available() \n
* If `_dataStream != NULL` it calls `_dataStream->available()`.
* @return result of `_dataStream->available()` or 0 if `_dataStream == NULL`.
*/
int Sodaq_WifiBee::available()
{
  if (_dataStream) {
    return _dataStream->available();
  } else {
    return 0;
  }
}

/*!
* Implementation of Stream::peek() \n
* If `_dataStream != NULL` it calls `_dataStream->peek()`.
* @return result of `_dataStream->peek()` or -1 if `_dataStream == NULL`.
*/
int Sodaq_WifiBee::peek()
{ 
  if (_dataStream) {
    return _dataStream->peek();
  } else {
    return -1;
  }
}

/*!
* Implementation of Stream::read() \n
* If `_dataStream != NULL` it calls `_dataStream->read()`.
* @return result of `_dataStream->read()` or -1 if `_dataStream == NULL`.
*/
int Sodaq_WifiBee::read()
{ 
  if (_dataStream) {
    return _dataStream->read();
  } else {
    return -1;
  }
}

/*!
* Implementation of Stream::flush() \n
* If `_dataStream != NULL` it calls `_dataStream->flush()`.
*/
void Sodaq_WifiBee::flush() {
  if (_dataStream) {
    _dataStream->flush();
  }
}

// Private methods
/*!
* This method reads and empties the input buffer of `_dataStream`. \n
* It attempts to output the data it reads to `_diagStream`. 
*/
void Sodaq_WifiBee::flushInputStream()
{
  while (available()) {
    diagPrint((char )read());
  }
}

/*!
* This method reads and empties the input buffer of '_dataStream'. \n
* It continues until the specified amount of time has elapsed. \n
* It attempts to output the data it reads to `_diagStream`.
* @param timeMS The time limit in milliseconds.
* @return The number of bytes it read.
*/
int Sodaq_WifiBee::skipForTime(const uint32_t timeMS)
{
  if (!_dataStream) {
    return 0;
  }

  int count = 0;
  uint32_t maxTS = millis() + timeMS;

  while (millis() < maxTS) {
    if (available()) {
      char c = read();
      diagPrint(c);
      count++;
    } else {
      _delay(10);
    }
  }

  return count;
}

/*!
* This method reads and empties the input buffer of '_dataStream'. \n
* It continues until it finds the specified prompt or until \n
* the specified amount of time has elapsed. \n
* It attempts to output the data it reads to `_diagStream`.
* @param prompt The prompt to read until.
* @param timeMS The time limit in milliseconds.
* @return `true` if it found the specified prompt within the time \n
* limit, otherwise `false`.
*/
bool Sodaq_WifiBee::skipTillPrompt(const char* prompt, const uint32_t timeMS)
{
  if (!_dataStream) {
    return false;
  }

  bool result = false;

  uint32_t maxTS = millis() + timeMS;
  size_t index = 0;
  size_t promptLen = strlen(prompt);

  while (millis() < maxTS) {
    if (available()) {
      char c = read();
      diagPrint(c);

      if (c == prompt[index]) {
        index++;

        if (index == promptLen) {
          result = true;
          break;
        }
      } else {
        index = 0;
      }
    } else {
      _delay(10);
    }
  }

  return result;
}

/*!
* This method reads one character from the input buffer of '_dataStream'. \n
* It continues until it reads one character or until the specified amount \n
* of time has elapsed. \n
* It attempts to output the data it read to `_diagStream`.
* @param data The character read is written to this parameter.
* @param timeMS The time limit in milliseconds.
* @return `true` if it successfully read one character within the \n
* time limit, otherwise `false`.
*/
bool Sodaq_WifiBee::readChar(char& data, const uint32_t timeMS)
{
  if (!_dataStream) {
    return false;
  }

  bool result = false;

  uint32_t maxTS = millis() + timeMS;
  while ((millis() < maxTS) && (!result)) {
    if (available()) {
      data = read();
      diagPrint(data);
      result = true;
    }
    else {
      _delay(10);
    }
  }

  return result;
}

/*!
* This method reads and empties the input buffer of '_dataStream'. \n
* It continues until it finds the specified prompt or until \n
* the specified amount of time has elapsed. \n
* It copies the read data into the buffer supplied. \n
* It attempts to output the data it reads to `_diagStream`.
* @param buffer The buffer to copy the data into.
* @param size The size of `buffer`.
* @param bytesStored The number of bytes copied is written to this parameter.
* @param prompt The prompt to read until.
* @param timeMS The time limit in milliseconds.
* @return `true` if it found the specified prompt within the time \n
* limit, otherwise `false`.
*/
bool Sodaq_WifiBee::readTillPrompt(uint8_t* buffer, const size_t size,
    size_t& bytesStored, const char* prompt, const uint32_t timeMS)
{
  if (!_dataStream) {
    return false;
  }

  bool result = false;

  uint32_t maxTS = millis() + timeMS;
  size_t promptIndex = 0;
  size_t promptLen = strlen(prompt);

  size_t bufferIndex = 0;
  size_t streamCount = 0;
  

  while (millis() < maxTS) {
    if (available()) {
      char c = read();
      diagPrint(c);

      streamCount++;

      if (bufferIndex < size) {
        buffer[bufferIndex] = c;
        bufferIndex++;
      }

      if (c == prompt[promptIndex]) {
        promptIndex++;

        if (promptIndex == promptLen) {
          result = true;
          bufferIndex = min(size - 1, streamCount - promptLen);
          break;
        }
      } else {
        promptIndex = 0;
      }
    } else {
      _delay(10);
    }
  }

  bytesStored = bufferIndex;

  return result;
}

/*!
* This method writes escaped ASCII data to `_dataStream`. \n
* It only escapes specific LUA characters.
* @param data The buffer containing the ASCII data to send.
*/
void Sodaq_WifiBee::sendEscapedAscii(const char* data)
{
  size_t length = strlen(data);

  //Todo add other lua escape characters?
  for (size_t i = 0; i < length; i++) {
    switch (data[i]) {
    case '\a':
      print("\\a");
      break;
    case '\b':
      print("\\b");
      break;
    case '\f':
      print("\\f");
      break;
    case '\n':
      print("\\n");
      break;
    case '\r':
      print("\\r");
      break;
    case '\t':
      print("\\t");
      break;
    case '\v':
      print("\\v");
      break;
    case '\\':
      print("\\\\");
      break;
    case '\"':
      print("\\\"");
      break;
    case '\'':
      print("\\\'");
      break;
    case '[':
      print("\\[");
      break;
    case ']':
      print("\\]");
      break;
    default:
      print(data[i]);
      break;
    }
  }
}

/*!
* This method writes escaped binary data to `_dataStream`. \n
* It numerically escpaes every byte.
* @param data The buffer containing the binary data to send.
* @param length The size of `data`.
*/
void Sodaq_WifiBee::sendEscapedBinary(const uint8_t* data, const size_t length)
{
  for (size_t i = 0; i < length; i++) {
    print("\\");
    print(data[i]);
  }
}

/*!
* This method opens a TCP or UDP connection to a remote server.
* @param server The server/host to connect to (IP address or domain).
* @param port The port to connect to.
* @param type The type of connection to establish, TCP or UDP.
* @return `true` if the connection was successfully established, \n
* otherwise `false`.
*/
bool Sodaq_WifiBee::openConnection(const char* server, const uint16_t port,
    const char* type)
{
  on();

  bool result;

  result = connect();

  if (result) {
    //Create the connection object
    print("wifiConn=net.createConnection(");
    print(type);
    println(", false)");
    skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

    //Setup the callbacks
    print("wifiConn:on(\"connection\", ");
    print(CONNECT_CALLBACK);
    println(")");
    skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

    print("wifiConn:on(\"reconnection\", ");
    print(RECONNECT_CALLBACK);
    println(")");
    skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

    print("wifiConn:on(\"disconnection\", ");
    print(DISCONNECT_CALLBACK);
    println(")");
    skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

    print("wifiConn:on(\"sent\", ");
    print(SENT_CALLBACK);
    println(")");
    skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

    print("wifiConn:on(\"receive\", ");
    print(RECEIVED_CALLBACK);
    println(")");
    skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

    print("wifiConn:connect(");
    print(port);
    print(",\"");
    print(server);
    println("\")");
    result = skipTillPrompt(CONNECT_PROMPT, SERVER_CONNECT_TIMEOUT);
  }

  return result;
}

/*!
* This method closes a TCP or UDP connection to a remote server.
* @return `true` if the connection was closed, otherwise `false`. \n
* It will return `false` if the connection was already closed.
*/
bool Sodaq_WifiBee::closeConnection()
{
  bool result;
  println("wifiConn:close()");
  result = skipTillPrompt(DISCONNECT_PROMPT, SERVER_DISCONNECT_TIMEOUT);

  off();

  return result;
}

/*!
* This method transmits ASCII data over an open TCP or UDP connection.
* @param data The data to transmit.
* @return `true` if the data was successfully transmitted, \n
* otherwise `false`.
*/
bool Sodaq_WifiBee::transmitAsciiData(const char* data)
{
  print("wifiConn:send(\"");
  sendEscapedAscii(data);
  println("\")");

  bool result;
  result = skipTillPrompt(SENT_PROMPT, RESPONSE_TIMEOUT);

  if (result) {
    if (skipTillPrompt(RECEIVED_PROMPT, SERVER_RESPONSE_TIMEOUT)) {
      readServerResponse();
    }
    else {
      clearBuffer();
    }
  }

  return result;
}

/*!
* This method transmits binary data over an open TCP or UDP connection.
* @param data The data to transmit.
* @return `true` if the data was successfully transmitted, \n
* otherwise `false`.
*/
bool Sodaq_WifiBee::transmitBinaryData(const uint8_t* data, const size_t length)
{
  print("wifiConn:send(\"");
  sendEscapedBinary(data, length);
  println("\")");

  bool result;
  result = skipTillPrompt(SENT_PROMPT, RESPONSE_TIMEOUT);

  if (result) {
    if (skipTillPrompt(RECEIVED_PROMPT, SERVER_RESPONSE_TIMEOUT)) {
      readServerResponse();
    } else {
      clearBuffer();
    }
  }

  return result;
}

/*!
* This method reads and stores the received response data.
* @return `true` on if it successfully reads the whole response, \n
* otherwise 'false'.
*/
bool Sodaq_WifiBee::readServerResponse()
{
  bool result;

  println(READ_BACK);
  result = skipTillPrompt(SOF_PROMPT, RESPONSE_TIMEOUT);
  
  if (result) {
    result = readTillPrompt(_buffer, _bufferSize, _bufferUsed, EOF_PROMPT,
      READBACK_TIMEOUT);
  }

  return result;
}

/*!
* This method joins the WifiBee to the network.
* @return `true` if the network was successfully joined, \n
* otherwise `false`.
*/
bool Sodaq_WifiBee::connect()
{
  println("wifi.setmode(wifi.STATION)");
  skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

  print("wifi.sta.config(\"");
  print(_APN);
  print("\",\"");
  print(_password);
  println("\")");
  skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

  println("wifi.sta.connect()");
  skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);

  return waitForIP(WIFI_CONNECT_TIMEOUT);
}

/*!
* This method disconnects the WifiBee from the network.
*/
void Sodaq_WifiBee::disconnect()
{
  println("wifi.sta.disconnect()");
  skipTillPrompt(LUA_PROMPT, RESPONSE_TIMEOUT);
}

/*!
* This method checks the connection status of the WifiBee.
* @param status The status code (0..5) is written to this parameter.
* @return `true` if it successfully read the status code, \n
* otherwise `false`.
*/
bool Sodaq_WifiBee::getStatus(uint8_t& status)
{
  bool result;
  
  println(STATUS_CALLBACK);
  result = skipTillPrompt(STATUS_PROMPT, RESPONSE_TIMEOUT);

  char statusCode;

  if (result) {
    result = readChar(statusCode, RESPONSE_TIMEOUT);
  }

  if (result) {
    if ((statusCode >= '0') && (statusCode <= '5')) {
      status = statusCode - '0';
    } else {
      result = false;
    }
  }
  
  return result;
}

/*! 
* This method repeatedly calls getStatus() to check the connection status.\n
* It continues until it the network has been joined or until the \n
* specified time limit has elapsed. \n
* @param timeMS The time limit in milliseconds.
* @return `true` if the Wifi network was joined, otherwise `false`.
*/
bool Sodaq_WifiBee::waitForIP(const uint32_t timeMS)
{
  bool result = false;

  uint8_t status = 1;
  uint32_t maxTS = millis() + timeMS;

  while ((millis() < maxTS) && (status == 1)) {
    skipForTime(STATUS_DELAY);
    getStatus(status);
  }

  // Without this small delay the lua interpreter sometimes
  // gets confused. This also flushes the incoming buffer
  skipForTime(100);

  //0 = Idle
  //1 = Connecting
  //2 = Wrong Credentials
  //3 = AP not found
  //4 = Connect Fail
  //5 = Got IP

  switch (status) {
  case 0:
    diagPrintLn("Failed to connect: Station idle");
    break;
  case 1:
    diagPrintLn("Failed to connect: Timeout");
    break;
  case 2:
    diagPrintLn("Failed to connect: Wrong credentials");
    break;
  case 3:
    diagPrintLn("Failed to connect: AP not found");
    break;
  case 4:
    diagPrintLn("Failed to connect: Connection failed");
    break;
  case 5:
    diagPrintLn("Success: IP received");
    result = true;
    break;
  }

  return result;
}

/*! This methods parses the HTTP response code from the data received.
* @param httpCode The response code is written into this parameter.
* @return `true` if the conversion returns a non-zero value, \n
* otherwise `false` .
*/
bool Sodaq_WifiBee::parseHTTPResponse(uint16_t& httpCode)
{
  bool result = false;

  // The HTTP response code should follow the first ' '
  if (_bufferUsed > 0) {
    char* codePos = strstr((char*)_buffer, " ");
    if (codePos) {
      httpCode = atoi(codePos);
      if (httpCode != 0) {
        result = true;
      }
    }
  }

  return result;
}

/*! This inline method clears the stored buffer.
*/
inline void Sodaq_WifiBee::clearBuffer()
{
  _bufferUsed = 0;
}

/*! This inline method is used throughout the class to add a delay.
*/
inline void Sodaq_WifiBee::_delay(uint32_t ms)
{
  delay(ms);
}
