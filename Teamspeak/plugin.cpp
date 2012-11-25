/*
 * TeamSpeak 3 demo plugin
 *
 * Copyright (c) 2008-2011 TeamSpeak Systems GmbH
 */
/*
left off at 4am 16 oct 2011
when speaker starts near you and runs away, he stays just as loud. you continue to fetch his metadata as he's speaking but it's always the same
if you run away from him while he's talking, he does get quieter
also, after he's run away, if he lets go of the mic and talks again, he'll be quiet like normal. perhaps the threaded updates aren't updating properly.
*/

#include <winsock2.h>
#ifdef _WIN32
#pragma warning (disable : 4100)  /* Disable Unreferenced parameter warning */
#include <Windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <functional>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/array.hpp>
#include <ctime>
#include <sstream>
#include <map>
#include <iostream>
using namespace std;
#include "public_errors.h"
//#include "public_errors_rare.h"
const unsigned int ERROR_database_empty_result = 0x0501; // fix from http://forum.teamspeak.com/showthread.php/68092-PLUGINSDK-public_errors_rare.h
#include "public_definitions.h"
#include "public_rare_definitions.h"
#include "ts3_functions.h"
#include "plugin_events.h"
#include "plugin.h"

static struct TS3Functions ts3Functions;

#ifdef _WIN32
#define _strcpy(dest, destSize, src) strcpy_s(dest, destSize, src)
#define snprintf sprintf_s
#else
#define _strcpy(dest, destSize, src) { strncpy(dest, src, destSize-1); dest[destSize-1] = '\0'; }
#endif

#define PLUGIN_API_VERSION 13

#define PATH_BUFSIZE 512
#define COMMAND_BUFSIZE 128
#define INFODATA_BUFSIZE 128
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128
#define REQUESTCLIENTMOVERETURNCODES_SLOTS 5

static char* pluginID = NULL;

/* Array for request client move return codes. See comments within ts3plugin_processCommand for details */
static char requestClientMoveReturnCodes[REQUESTCLIENTMOVERETURNCODES_SLOTS][RETURNCODE_BUFSIZE];

#ifdef _WIN32
/* Helper function to convert wchar_T to Utf-8 encoded strings on Windows */
static int wcharToUtf8(const wchar_t* str, char** result) {
	int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
	*result = (char*)malloc(outlen);
	if(WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0) {
		*result = NULL;
		return -1;
	}
	return 0;
}
#endif

typedef boost::shared_ptr<boost::asio::ip::tcp::socket> SmartSocket;
vector<float> vListenerPosition; // yes, a global. sue me.
bool bRunServer, bDead;
boost::thread hServerThread;
map<anyID, pair<bool, boost::thread>> speakingThreads;
const char* szAddress = DEFAULT_ADDRESS;
int iPort = DEFAULT_PORT;

/*********************************** Required functions ************************************/
/*
 * If any of these required functions is not implemented, TS3 will refuse to load the plugin
 */

/* Unique name identifying this plugin */
const char* ts3plugin_name() {
#ifdef _WIN32
	/* TeamSpeak expects UTF-8 encoded characters. Following demonstrates a possibility how to convert UTF-16 wchar_t into UTF-8. */
	static char* result = NULL;  /* Static variable so it's allocated only once */
	if(!result) {
		const wchar_t* name = L"Voice Proximity";
		if(wcharToUtf8(name, &result) == -1) {  /* Convert name into UTF-8 encoded result */
			result = "Voice Proximity";  /* Conversion failed, fallback here */
		}
	}
	return result;
#else
	return "Voice Proximity";
#endif
}

/* Plugin version */
const char* ts3plugin_version() {
    return "Alpha 1.0";
}

/* Plugin API version. Must be the same as the clients API major version, else the plugin fails to load. */
int ts3plugin_apiVersion() {
	return PLUGIN_API_VERSION;
}

/* Plugin author */
const char* ts3plugin_author() {
	/* If you want to use wchar_t, see ts3plugin_name() on how to use */
    return "29th Engineer Corps";
}

/* Plugin description */
const char* ts3plugin_description() {
	/* If you want to use wchar_t, see ts3plugin_name() on how to use */
    return "Visit us at www.29th.org";
}

/* Set TeamSpeak 3 callback functions */
void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    ts3Functions = funcs;
}

/*
 * Custom code called right after loading the plugin. Returns 0 on success, 1 on failure.
 * If the function returns 1 on failure, the plugin will be unloaded again.
 */
int ts3plugin_init() {
    char appPath[PATH_BUFSIZE];
    char resourcesPath[PATH_BUFSIZE];
    char configPath[PATH_BUFSIZE];
	char pluginPath[PATH_BUFSIZE];

    /* Your plugin init code here */
    printf("PLUGIN: init\n");

	/* Show API versions */
	printf("PLUGIN: Client API Version: %d, Plugin API Version: %d\n", ts3Functions.getAPIVersion(), ts3plugin_apiVersion());

    /* Example on how to query application, resources and configuration paths from client */
    /* Note: Console client returns empty string for app and resources path */
    ts3Functions.getAppPath(appPath, PATH_BUFSIZE);
    ts3Functions.getResourcesPath(resourcesPath, PATH_BUFSIZE);
    ts3Functions.getConfigPath(configPath, PATH_BUFSIZE);
	ts3Functions.getPluginPath(pluginPath, PATH_BUFSIZE);

	printf("PLUGIN: App path: %s\nResources path: %s\nConfig path: %s\nPlugin path: %s\n", appPath, resourcesPath, configPath, pluginPath);

	/* Initialize return codes array for requestClientMove */
	memset(requestClientMoveReturnCodes, 0, REQUESTCLIENTMOVERETURNCODES_SLOTS * RETURNCODE_BUFSIZE);

	// Start TCP Server
	//startTcpServer("127.0.0.1", 25637);
	if(ENABLE_TCP_SERVER) {
		bRunServer = true;
		hServerThread = boost::thread(startTcpServer, DEFAULT_ADDRESS, DEFAULT_PORT);
	}

    return 0;  /* 0 = success, 1 = failure */
}



/* Custom code called right before the plugin is unloaded */
void ts3plugin_shutdown() {
    /* Your plugin cleanup code here */
    printf("PLUGIN: shutdown\n");

	/*
	 * Note:
	 * If your plugin implements a settings dialog, it must be closed and deleted here, else the
	 * TeamSpeak client will most likely crash (DLL removed but dialog from DLL code still open).
	 */
	/*if (hSpeakerThread != 0) {
		WaitForSingleObject (hSpeakerThread, INFINITE);
		CloseHandle (hSpeakerThread);
	}
	if (hServerThread != 0) {
		WaitForSingleObject (hServerThread, INFINITE);
		CloseHandle (hServerThread);
	}*/

	/* Free pluginID if we registered it */
	if(pluginID) {
		free(pluginID);
		pluginID = NULL;
	}

	// Close all speaker threads
	map<anyID, pair<bool, boost::thread>>::iterator it;
	for(it=speakingThreads.begin(); it != speakingThreads.end(); it++) {
		it->second.first = false;
		it->second.second.join();
	}

	// Close server thread
	if(ENABLE_TCP_SERVER) {
		bRunServer = false;
		hServerThread.join();
	}
}

/****************************** Optional functions ********************************/
/*
 * Following functions are optional, if not needed you don't need to implement them.
 */

/*
 * Implement the following three functions when the plugin should display a line in the server/channel/client info.
 * If any of ts3plugin_infoTitle, ts3plugin_infoData or ts3plugin_freeMemory is missing, the info text will not be displayed.
 */

/* Static title shown in the left column in the info frame */
const char* ts3plugin_infoTitle() {
	return "Voice Proximity";
}

/* Required to release the memory for parameter "data" allocated in ts3plugin_infoData */
void ts3plugin_freeMemory(void* data) {
	free(data);
}

/*
 * Plugin requests to be always automatically loaded by the TeamSpeak 3 client unless
 * the user manually disabled it in the plugin dialog.
 * This function is optional. If missing, no autoload is assumed.
 */
int ts3plugin_requestAutoload() {
	return 0;  /* 1 = request autoloaded, 0 = do not request autoload */
}

/************************** TeamSpeak callbacks ***************************/
/*
 * Following functions are optional, feel free to remove unused callbacks.
 * See the clientlib documentation for details on each function.
 */

/* Clientlib */

/*int ts3plugin_onTextMessageEvent(uint64 serverConnectionHandlerID, anyID targetMode, anyID toID, anyID fromID, const char* fromName, const char* fromUniqueIdentifier, const char* message, int ffIgnored) {
}*/

/*void onCustom3dRolloffCalculationClientEvent(uint64 serverConnectionHandlerID, anyID clientID, float distance, float* volume) {
	ts3Functions.logMessage("onCustom3dRolloffCalculationClientEvent()", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID); // doesn't get called
}*/

/*void ts3plugin_onTalkStatusChangeEvent(uint64 serverConnectionHandlerID, int status, int isReceivedWhisper, anyID clientID) {
	anyID myID;
	ts3Functions.getClientID(serverConnectionHandlerID, &myID);

	if(clientID != myID && status == STATUS_TALKING && !vListenerPosition.empty()) {
		if(setSpeakerPosition(serverConnectionHandlerID, clientID) != ERROR_ok) {
			ts3Functions.logMessage("ERROR Setting Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
			return;
		}
		//ts3Functions.logMessage("Set Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);

		hSpeakerThread = CreateThread(0, 0, SpeakerThread, (LPVOID) clientID, 0, 0);
	}
	else if(clientID != myID && status == STATUS_NOT_TALKING) {
		//ts3Functions.logMessage("Stopped Talking", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
	}
}*/

void ts3plugin_onTalkStatusChangeEvent(uint64 serverConnectionHandlerID, int status, int isReceivedWhisper, anyID clientID) {
	anyID myID;
	ts3Functions.getClientID(serverConnectionHandlerID, &myID);

	if(clientID != myID && status == STATUS_TALKING && !vListenerPosition.empty()) {
		// First check if speaker has meta data. Otherwise they're probably not in-game, so we don't want to run threads for them.
		char* chMetaData;
		ts3Functions.getClientVariableAsString(serverConnectionHandlerID, clientID, CLIENT_META_DATA, &chMetaData);
		if(strcmp(chMetaData, "") != 0) {
			//ts3Functions.logMessage("Starting Thread", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
			speakingThreads[clientID].first = true;
			speakingThreads[clientID].second = boost::thread(speakerActiveThread, serverConnectionHandlerID, clientID);
		}
		//ts3Functions.logMessage("Speaker Meta Data Empty", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
	}
	else if(clientID != myID && status == STATUS_NOT_TALKING) {
		if(speakingThreads.find(clientID) != speakingThreads.end()) {
			speakingThreads[clientID].first = false;
			//ts3Functions.logMessage("Interrupted Speaking Thread", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
		}
	}
}


/*void ts3plugin_onTalkStatusChangeEvent(uint64 serverConnectionHandlerID, int status, int isReceivedWhisper, anyID clientID) {
	anyID myID;
	ts3Functions.getClientID(serverConnectionHandlerID, &myID);

	if(clientID != myID && !vListenerPosition.empty() && status == STATUS_TALKING) {
		if(setSpeakerPosition(serverConnectionHandlerID, clientID) != ERROR_ok) {
			ts3Functions.logMessage("ERROR Setting Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
			return;
		}
		ts3Functions.logMessage("Set Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
	}
}*/

/* Clientlib rare */

/*int ts3plugin_onClientPokeEvent(uint64 serverConnectionHandlerID, anyID fromClientID, const char* pokerName, const char* pokerUniqueIdentity, const char* message, int ffIgnored) {
}*/

// Enable with ClientQuery Plugin Usage
void ts3plugin_onClientSelfVariableUpdateEvent(uint64 serverConnectionHandlerID, int flag, const char* oldValue, const char* newValue) {
	if(!ENABLE_TCP_SERVER && flag == CLIENT_META_DATA && strcmp(oldValue, newValue) != 0) {
		receiveNewPosition(newValue, serverConnectionHandlerID);
	}
}

/* 29th Functions */

void receiveNewPosition(const char* metaData, uint64 serverConnectionHandlerID) {
	vector<float> vForward, vUp;
	if(serverConnectionHandlerID == 0) {
		serverConnectionHandlerID = ts3Functions.getCurrentServerConnectionHandlerID(); // needed for systemset3DListenerAttributes()
	}

	// Set meta data so other clients can read your new position (Disable with ClientQuery Plugin Usage)
	if(ENABLE_TCP_SERVER) {
		if(ts3Functions.setClientSelfVariableAsString(serverConnectionHandlerID, CLIENT_META_DATA, metaData) != ERROR_ok) // not being set to server
			ts3Functions.logMessage("Error setting self meta data", LogLevel_ERROR, "Plugin", 0);
		ts3Functions.flushClientSelfUpdates(serverConnectionHandlerID, NULL);
	}
	
	// Empty cache
	vListenerPosition.clear(); // empty it, otherwise it will just add 3 floats to it

	if(strcmp(metaData, KEYWORD_DEAD) == 0) {
		bDead = true;
		TS3_VECTOR forward = {0.0f, 0.0f, 0.0f};
		TS3_VECTOR up = {0.0f, 0.0f, 0.0f};
		if(ts3Functions.systemset3DListenerAttributes(serverConnectionHandlerID, NULL, &forward, &up) != ERROR_ok) {
			ts3Functions.logMessage("Error zeroing listener position", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			return;
		}
	}
	else {
		if(bDead)
			bDead = false;

		// Parse data into 3 separate vectors
		if(getListenerPosition(metaData, vListenerPosition, vForward, vUp) != ERROR_ok) {
			ts3Functions.logMessage("Error getting listener position", LogLevel_ERROR, "Plugin", 0);
			return;
		}
		/*char output[64];
		sprintf_s(output, "%f,%f,%f", vListenerPosition[0], vListenerPosition[1], vListenerPosition[2]);
		ts3Functions.logMessage(output, LogLevel_DEBUG, "Plugin", 0);*/

		// Convert to struct
		TS3_VECTOR position = {0.0f, 0.0f, 0.0f};
		TS3_VECTOR forward = {vForward[0], vForward[1], vForward[2]};
		TS3_VECTOR up = {vUp[0], vUp[1], vUp[2]}; // inverse because by default left=right & right=left
		//TS3_VECTOR up = {vUp[0] * -1.0f, vUp[1] * -1.0f, vUp[2] * -1.0f}; // inverse because by default left=right & right=left

		// Set listener position/forward/up
		if(ts3Functions.systemset3DListenerAttributes(serverConnectionHandlerID, &position, &forward, &up) != ERROR_ok) {
			ts3Functions.logMessage("Error setting listener position", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			return;
		}
		//ts3Functions.logMessage("Set listener position/forward/up", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
	}
}

int makePositionRelative(vector<float> speaker, vector<float>& newSpeaker) {
	size_t i;
	for(i = 0; i < speaker.size(); i++) {
		newSpeaker.push_back((speaker[i] - vListenerPosition[i]) / 60.352f);
	}
	if(newSpeaker.size() == 3) {
		return ERROR_ok;
	}
	return 1;
}

// Need listener clientID
int getListenerPosition(const char* chMetaData, vector<float>& vPosition, vector<float>& vForward, vector<float>& vUp) {
	vector<string> strParts, strPosition, strForward, strUp;
	string strMetaData;

	strMetaData = string(chMetaData);

	// Parse meta data
	removeBrackets(strMetaData);
	split(strMetaData, '@', strParts);

	if(strParts.size() != 3) {
		ts3Functions.logMessage("getListenerPosition() strParts.size() != 3", LogLevel_ERROR, "Plugin", 0);
		return 1;
	}

	split(strParts[0], ',', strPosition); // strPosition.size() = 3
	split(strParts[1], ',', strForward);
	split(strParts[2], ',', strUp);
	vStringToFloat(strPosition, vPosition);
	vStringToFloat(strForward, vForward);
	vStringToFloat(strUp, vUp);

	if(vPosition.size() != 3 || vForward.size() != 3 || vUp.size() != 3) {
		ts3Functions.logMessage("getListenerPosition() position, forward, or up size != 3", LogLevel_ERROR, "Plugin", 0);
		return 1;
	}
	return ERROR_ok;
}

int setSpeakerPosition(uint64 serverConnectionHandlerID, anyID clientID) {
	char* chMetaData;
	TS3_VECTOR position = {0.0f, 0.0f, 0.0f};
	
	// Get speaker's meta data
	ts3Functions.getClientVariableAsString(serverConnectionHandlerID, clientID, CLIENT_META_DATA, &chMetaData);

	// If speaker is dead but i'm not, set out of range
	if(strcmp(chMetaData, KEYWORD_DEAD) == 0 && !bDead) {// || strcmp(chMetaData, KEYWORD_DEAD) != 0 && bDead) {
		position.x = 999.0f;
		position.y = 999.0f;
		position.z = 999.0f;
		ts3Functions.logMessage("Making speaker out of range", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
	}
	// If we're both alive, set proper position
	else if(strcmp(chMetaData, KEYWORD_DEAD) != 0 && !bDead) {
		string strMetaData;
		vector<string> strParts, strPosition;
		vector<float> vPosition, vRelativePosition;

		strMetaData = string(chMetaData);
		//ts3Functions.logMessage(chMetaData, LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);

		if(strMetaData.length() == 0) {
			ts3Functions.logMessage("setSpeakerPosition() Meta data empty", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			return 1;
		}
	
		// Parse meta data
		removeBrackets(strMetaData);
		split(strMetaData, '@', strParts);
		if(strParts.size() != 3) {
			ts3Functions.logMessage("Error parsing speaker's meta data parts", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			return 1;
		}
		split(strParts[0], ',', strPosition); // only need position
		vStringToFloat(strPosition, vPosition);
	
		// Make position relative to 0.0, 0.0, 0.0
		if(makePositionRelative(vPosition, vRelativePosition) != ERROR_ok) {
			ts3Functions.logMessage("Error making position relative", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			return 1;
		}

		position.x = vRelativePosition[0];
		position.y = vRelativePosition[1];
		position.z = vRelativePosition[2]; // try changing Z to 0.0f
	}
	// otherwise we're both dead so set position to 0,0,0

	// Tell TS3 the speaker's position
	if(ts3Functions.channelset3DAttributes(serverConnectionHandlerID, clientID, &position) != ERROR_ok) {
        ts3Functions.logMessage("Error setting speaker's 3d location", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
		return 1;
	}
	return ERROR_ok;
}

/* 29th Threads */

void speakerActiveThread(uint64 serverConnectionHandlerID, anyID clientID) {
	while(speakingThreads[clientID].first) {
		if(setSpeakerPosition(serverConnectionHandlerID, clientID) != ERROR_ok) {
			ts3Functions.logMessage("ERROR Setting Speaker Position", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
		}
		//ts3Functions.logMessage("Set Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
		Sleep(UPDATE_SPEAKER_MS);
	}
}

/*DWORD WINAPI SpeakerThread(LPVOID lpParam) {
	anyID clientID = (anyID) lpParam;
	uint64 serverConnectionHandlerID = ts3Functions.getCurrentServerConnectionHandlerID();
	char* flag;
	int interval = 1;
	clock_t now = 0;
	clock_t last = clock();

	while(true) {
		now = clock();
		if((now - last) >= (interval * CLOCKS_PER_SEC)) // sec has passed...
		{
			//ts3Functions.logMessage("test", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
			ts3Functions.getClientVariableAsString(serverConnectionHandlerID, clientID, CLIENT_FLAG_TALKING, &flag);
			//ts3Functions.logMessage(flag, LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
			//ts3Functions.getClientVariableAsInt(serverConnectionHandlerID, clientID, CLIENT_FLAG_TALKING, flag);
			if(strcmp(flag, "1") == 0) {
				if(setSpeakerPosition(serverConnectionHandlerID, clientID) != ERROR_ok) {
					ts3Functions.logMessage("ERROR Setting Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
					return 1;
				}
				ts3Functions.logMessage("Set Speaker Position", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
			}
			else {
				//ts3Functions.logMessage("Breaking", LogLevel_DEBUG, "Plugin", serverConnectionHandlerID);
				break;
			}

			// reset our clocks..
			last = now;
			now = 0;
		}
	}
	return 0;
}*/

/*void server()
{
	boost::asio::io_service ios;
 
	boost::asio::ip::tcp::acceptor acp(ios, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), ECHO_PORT));
 
	while(bRunServer)
	{
		SmartSocket sock(new boost::asio::ip::tcp::socket(ios));
		ts3Functions.logMessage("Waiting for a new connection", LogLevel_DEBUG, "Plugin", 0);
		acp.accept(*sock);
		ts3Functions.logMessage("Connection accepted", LogLevel_DEBUG, "Plugin", 0);
		boost::thread t(session, sock);
	}
}

void session(SmartSocket sock)
{
	try
	{
		bool pending = true;
		while(pending)
		{
			boost::array<char, MSG_LEN> data;
 
			boost::system::error_code error;
			size_t length = sock->read_some(boost::asio::buffer(data), error);
			if (error == boost::asio::error::eof)
				break; // Connection closed cleanly by peer.
			else if (error)
			throw boost::system::system_error(error); // Some other error.
 
			if(data[length-1] == '\0') // 1.
			{
				std::cout << "Client sent a terminator" << std::endl;
				--length;
				pending = false;
			}
 
			if(length) // 2.
			{
				std::cout << "echoing " << length << " characters" << std::endl;
				boost::asio::write(*sock, boost::asio::buffer(data, length));
			}
		}
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception in thread: " << e.what() << std::endl;
	}
}

void tcpServer() {
	using boost::asio::ip::tcp;
	try {
		boost::asio::io_service io;
		tcp::acceptor acceptor(io, tcp::endpoint(boost::asio::ip::address::from_string(DEFAULT_ADDRESS), DEFAULT_PORT));//tcp::v4(), 13));
		tcp::socket socket(io);

		ts3Functions.logMessage("Server Initialized", LogLevel_DEBUG, "Plugin", 0);
		while(bRunServer)
		{
			acceptor.accept(socket);
			std::string message = "test";

			//boost::system::error_code ignored_error;
			boost::asio::write(socket, boost::asio::buffer(message));//, boost::asio::transfer_all(), ignored_error);
			socket.close();
		}
	}
	catch (std::exception& e) {
		ts3Functions.logMessage(e.what(), LogLevel_ERROR, "Plugin", 0);
	}
}*/

void clientThread(SOCKET sock) {
	char szBuff[DEFAULT_BUFFER];//, trimmedMsg[DEFAULT_BUFFER];
	int ret;

	while(bRunServer) {
		ret = recv(sock, szBuff, DEFAULT_BUFFER, 0);
		if (ret > 0 && ret != SOCKET_ERROR) {
			szBuff[ret] = '\0';
			if(szBuff[ret-1] == '\n')
				szBuff[ret-1] = '\0';

			//strcpy_s(trimmedMsg, string(szBuff).substr(0, ret).c_str());
			//ts3Functions.logMessage(szBuff, LogLevel_DEBUG, "Plugin", 0);
			receiveNewPosition(szBuff, 0);
		}
	}
	shutdown(sock, SD_SEND);
	closesocket(sock);
}

void startTcpServer(const char* szAddress, int iPort) {
	WSADATA       wsd;
	SOCKET        sListen, sClient;
	struct sockaddr_in local;
	boost::thread hClientThread;
	
	if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) {
		ts3Functions.logMessage("Failed to load Winsock!", LogLevel_ERROR, "Plugin", 0);
		return;
	}

	// Create our listening socket
	sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (sListen == SOCKET_ERROR) {
		ts3Functions.logMessage("socket() failed", LogLevel_ERROR, "Plugin", 0);
		return;
	}

	// Select the local interface and bind to it
	local.sin_addr.s_addr = inet_addr(szAddress);
	if (local.sin_addr.s_addr == INADDR_NONE)
		ExitProcess(1);
	local.sin_family = AF_INET;
	local.sin_port = htons(iPort);

	if (::bind(sListen, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) {
		ts3Functions.logMessage("bind() failed", LogLevel_ERROR, "Plugin", 0);
		return;
	}
	listen(sListen, 8);

	// Set socket non-blocking
	u_long iMode=1;
	ioctlsocket(sListen, FIONBIO, &iMode);

	// In a continous loop, wait for incoming clients. Once one
	// is detected, create a thread and pass the handle off to it.
	while (bRunServer) {
		sClient = accept(sListen, NULL, NULL);//, (struct sockaddr *)&client, &iAddrSize);
		if (sClient != INVALID_SOCKET) {
			ts3Functions.logMessage("Accepted client", LogLevel_DEBUG, "Plugin", 0);
			hClientThread = boost::thread(clientThread, sClient);
		}
	}
	hClientThread.join();
	closesocket(sListen);

	WSACleanup();
}

/* 29th Stock Functions */

void ltrim(string& str, char c) {
	size_t startpos = str.find_first_not_of(c);
	if( string::npos != startpos )
	{
		str = str.substr( startpos );
	}
}

void rtrim(string& str, char c) {
	size_t endpos = str.find_last_not_of(c);
	if( string::npos != endpos )
	{
		str = str.substr( 0, endpos+1 );
	}
}

void removeBrackets(string& str) {
	ltrim(str, '[');
	rtrim(str, ']');
}

void split(const string& s, char c, vector<string>& v) {
	string::size_type i = 0;
	string::size_type j = s.find(c);
	while (j != string::npos) {
		v.push_back(s.substr(i, j-i));
		i = ++j;
		j = s.find(c, j);
		if (j == string::npos)
			v.push_back(s.substr(i, s.length( )));
	}
}

// may be better to do atof(std::basic_string.c_str()) even though that goes 'double' first
inline float convertToFloat(std::string const& s) {
	/*std::istringstream i(s);
	float x;
	i >> x;
	return x;*/
	return (float) atof(s.c_str());
}

void vStringToFloat(vector<string> s, vector<float>& f) {
	size_t i;
	for(i = 0; i< s.size(); i++) {
		f.push_back(convertToFloat(s[i]));
	}
}