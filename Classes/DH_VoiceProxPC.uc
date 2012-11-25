class DH_VoiceProxPC extends DHPlayer;

var DH_VoiceProxTCP TCP;
var float LastUpdatePositionTime, UpdatePositionInterval;
var int UpdateCounter;
var int MarcoPoloInterval;

simulated event PostBeginPlay()
{
	Super.PostBeginPlay();
	TCP=Spawn(class'DH_VoiceProxTCP', self); // Instanciate MyTCP class
	TCP.InitTcpLinkEventLogger();  // Init the connection
}

state PlayerWalking
{
	simulated function BeginState()
	{
		Super.BeginState();
		if(Level.NetMode != NM_DedicatedServer)
		{
			SetTimer(UpdatePositionInterval, true);
		}
	}

	simulated function Timer()
	{
		Super.Timer();
		UpdatePosition();
	}
}

state Dead
{
	simulated function BeginState()
	{
		Super.BeginState();
		ErasePosition();
	}
}

state Spectating
{
	simulated function BeginState()
	{
		Super.BeginState();
		ErasePosition();
	}
}

state GameEnded
{
	simulated function BeginState()
	{
		Super.BeginState();
		ErasePosition();
	}
}

function UpdatePosition()
{
	local vector PlayerLocation;
	local float PlayerHeading;
	local string LogString;
	local rotator PlayerRotation;
	local vector X, Y, Z;

	if(TCP != None) {
		UpdateCounter++;

		if(UpdateCounter >= MarcoPoloInterval) {
			LogString = "clientupdate client_meta_data=MARCO";
			Log("Sending MARCO");
			UpdateCounter = 0;
			TCP.SendText(LogString);
			LogString = "client
		}
		else {
			LogString = "clientupdate client_meta_data=";

			if(Pawn != None || Pawn.Health < 1)
			{
				PlayerLocation = Pawn.Location;
				PlayerHeading = Pawn.Rotation.Yaw;
				PlayerRotation = Pawn.GetViewRotation();
				GetAxes(PlayerRotation, X, Y, Z);
				LogString = LogString $ "[" $ PlayerLocation $ "@" $ X $ "@" $ Z $ "]";
			}
			TCP.SendText(LogString);
		}

		//Log("Sent text to TCP server: " $ LogString);
	}
}

function ReceiveText(string Text) {
	Log("Received Text: " $ Text);
	ClientMessage("Received Text: " $ Text);
}

exec function TSConnect() {
	if(TCP != None) {
		TCP.InitTcpLinkEventLogger();  // Init the connection
	}
}

function ErasePosition()
{
	local string LogString;
	LogString = "clientupdate client_meta_data=DEAD";
	//LogString = "DEAD";
	TCP.SendText(LogString);
}

defaultproperties
{
	UpdatePositionInterval=0.500000
	MarcoPoloInterval=10 // every X update
}