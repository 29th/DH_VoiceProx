class DH_VoiceProxTCP extends TcpLink;
 
var int  RemotePort;    // Port of the server to connect
var string ServerAddress; // Address of the server to connect  // Seems to be string in UT2004
var bool bDebug;
 
//------------------------------------------------------------------------------
 
// First function called (init of the TCP connection)
Function InitTcpLinkEventLogger() {

	Log("InitTcpLinkEventLogger: Will start TCP connection!");

	Resolve(ServerAddress);   // Resolve the address of the server
}                     
 
//------------------------------------------------------------------------------
 
Function Cclose() { // Closes the current connection.
	Close();
}
 
//------------------------------------------------------------------------------
 
Function int SendText (coerce string Str) { // Send the string "Str" + "line feed" char
	local int result;

	result = super.SendText(Str);  // Call the super (send the text)
	if(bDebug)
		Log ("SentText: " $Str$" , (Number of bytes sent: "$result$")");
	return result;
}
 
//--EVENTS--
 
// Called when the IP is resolved
Event Resolved( IpAddr Addr ) {
	Log("OK, Address resolved");
	Addr.Port = remotePort;
	BindPort();         // In UnrealTournament, the CLIENT has to make a bind to create a socket! (Not as a classic TCP connection!!!)

	ReceiveMode = RMODE_Event;  // Incomming messages are triggering events
	LinkMode = MODE_Text;       // We expect to receive text (if we receive data)
	Open(Addr);                 // Open the connection

	Log ("Connected => Port: "$Addr.port$" Address: "$Addr.Addr);
	ROPlayer(Owner).ClientMessage("Connected to Teamspeak");
}
 
//------------------------------------------------------------------------------
 
// If the IP was not resolved...
Event ResolveFailed() {
	Log("### Error, resolve failed!!!");
}
 
//------------------------------------------------------------------------------
 
event Opened() {
	Log("Ok, connection opened");
}
 
//------------------------------------------------------------------------------
 
// If somebody has close the connection...
Event Closed() {
	Log("Connection closed");
}
 
//------------------------------------------------------------------------------
 
// Called when a string is received
Event ReceivedText (string Text) {
	// We have just received a string !
	if(bDebug) Log("Read string: "$Text$" Size : "$Len(Text));

	DH_VoiceProxPC(Owner).ReceiveText(Text);
}
 
//------------------------------------------------------------------------------
 
defaultproperties
{
	ServerAddress="127.0.0.1"  // Remote server address
	//remotePort=5150
	remotePort=25639            // Remote port number
}