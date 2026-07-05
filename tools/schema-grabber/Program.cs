using System.Net;
using System.Text.RegularExpressions;

using ProtoBuf;
using SteamKit2;
using SteamKit2.Internal;
using SteamKit2.Authentication;

namespace SchemaGrabber
{
    public sealed class GetUserStatsCallback : CallbackMsg
    {
        public CMsgClientGetUserStatsResponse body;

        internal GetUserStatsCallback(IPacketMsg packetMsg)
        {
            var statsResponse = new ClientMsgProtobuf<CMsgClientGetUserStatsResponse>(packetMsg);
            this.JobID = statsResponse.TargetJobID;
            body = statsResponse.Body;
        }
    }

    public sealed partial class GetUserStatsCallbackHandler : ClientMsgHandler
    {
        public override void HandleMsg(IPacketMsg msg)
        {
            CallbackMsg callback = null;

            switch (msg.MsgType)
            {
                case EMsg.ClientGetUserStatsResponse:
                    callback = new GetUserStatsCallback(msg);
                    break;
            }

            if (callback != null)
            {
                this.Client.PostCallback(callback);
            }
        }
    }

    class SchemaGrabber
    {
        //Thanks Parasitic-Hollow for discovering this endpoint
        public const string ReviewUrl = "https://store.steampowered.com/appreviews/{appId}?json=1&filter=recent&language=all&purchase_type=all&num_per_page=100";

        public readonly string GuardDataDir = Path.Combine(Environment.CurrentDirectory, "Sentries");
        public readonly string TicketDir = Path.Combine(Environment.CurrentDirectory, "Tickets");

        public readonly string StatsDir = Path.Combine(Environment.GetEnvironmentVariable("HOME"), ".steam/steam/appcache/stats");

        protected bool finished = false;
        protected CallbackManager callbackManager;

        protected string guardDataFile => Path.Combine(GuardDataDir, Username + ".sentry");

        protected string getStoredGuardData()
        {
            if (!Directory.Exists(GuardDataDir))
            {
                Directory.CreateDirectory(GuardDataDir);
            }

            if (!File.Exists(guardDataFile))
            {
                return null;
            }

            return File.ReadAllText(guardDataFile);
        }

        protected void storeGuardData(string guardData)
        {
            if (!File.Exists(guardDataFile))
            {
                File.Create(guardDataFile).Close();
            }

            File.WriteAllText(guardDataFile, guardData);
        }

        public readonly string Username;
        public readonly string Password;
        public readonly uint TargetAppId;

        public SteamClient Client;
        public SteamUser User;
        public SteamFriends Friends;
        public SteamApps Apps;

        public SchemaGrabber(string username, string password, uint targetAppId)
        {
            Username = username;
            Password = password;
            TargetAppId = targetAppId;

            Client = new SteamClient();
            callbackManager = new CallbackManager(Client);

            User = Client.GetHandler<SteamUser>();
            Friends = Client.GetHandler<SteamFriends>();
            Apps = Client.GetHandler<SteamApps>();

            if
            (
                User == null
                || Friends == null
                || Apps == null
            )
            {
                Console.WriteLine("Failed to get Handlers!");
                Environment.Exit(1);
            }

            Client.AddHandler(new GetUserStatsCallbackHandler());

            callbackManager.Subscribe<SteamClient.ConnectedCallback>(OnConnected);
            callbackManager.Subscribe<SteamClient.DisconnectedCallback>(OnDisconnected);

            callbackManager.Subscribe<SteamUser.LoggedOnCallback>(OnLoggedOn);
            callbackManager.Subscribe<SteamUser.LoggedOffCallback>(OnLoggedOff);
            callbackManager.Subscribe<SteamUser.AccountInfoCallback>(OnAccountInfo);
        }

        async protected void OnConnected(SteamClient.ConnectedCallback cb)
        {
            Console.WriteLine("Connected to Steam! Logging in...");

            var details = new SteamKit2.Authentication.AuthSessionDetails
            {
                Username = Username,
                Password = Password,
                IsPersistentSession = true,
                Authenticator = new UserConsoleAuthenticator()
            };

            var guardData = getStoredGuardData();
            if (guardData != null)
            {
                //TODO: Add to details directly?
                details.GuardData = guardData;
            }

            var authSess = await Client.Authentication.BeginAuthSessionViaCredentialsAsync(details);
            var resp = await authSess.PollingWaitForResultAsync();

            if (resp.NewGuardData != null)
            {
                storeGuardData(resp.NewGuardData);
            }

            User.LogOn(new SteamUser.LogOnDetails
            {
                Username = resp.AccountName,
                AccessToken = resp.RefreshToken,
                ShouldRememberPassword = true
            });
        }

        protected void OnDisconnected(SteamClient.DisconnectedCallback cb)
        {
            Console.WriteLine("Disconnected from Steam! Exiting...");
        }

        protected void OnLoggedOn(SteamUser.LoggedOnCallback cb)
        {
            Console.WriteLine($"Logged in as {Username}");
        }

        protected void OnLoggedOff(SteamUser.LoggedOffCallback cb)
        {
            Console.WriteLine($"Logged out from {Username}");
        }

        protected void OnAccountInfo(SteamUser.AccountInfoCallback cb)
        {
            Console.WriteLine("Account Info received! Grabbing schema...");
            GetSchema();
        }

        public AsyncJob<GetUserStatsCallback> GetUserStats(ulong steamId, uint appId)
        {
            var msg = new ClientMsgProtobuf<CMsgClientGetUserStats>(EMsg.ClientGetUserStats);
            msg.SourceJobID = Client.GetNextJobID();

            msg.Body.game_id = appId;
            msg.Body.schema_local_version = -1;
            msg.Body.steam_id_for_user = steamId;

            Client.Send(msg);

            return new AsyncJob<GetUserStatsCallback>(Client, msg.SourceJobID);
        }

        protected async void GetSchema()
        {
            var uri = ReviewUrl.Replace("{appId}", TargetAppId.ToString());
            Console.WriteLine($"Downloading {uri}");

            using (var wc = new HttpClient())
            {
                var json = await wc.GetStringAsync(uri);
                var re = new Regex("(?<=\"steamid\":\")[0-9]+(?=\")");
                var matches = re.Matches(json);
                Console.WriteLine($"Got {matches.Count} steamIds");

                foreach (var match in matches)
                {
                    var steamIdStr = match.ToString();
                    if (steamIdStr == null)
                    {
                        continue;
                    }

                    var steamId = ulong.Parse(steamIdStr);
                    var resp = await GetUserStats(steamId, TargetAppId);
                    var body = resp.body;

                    Console.WriteLine($"GetUserStatsResponse {resp.body.eresult}");
                    if ((EResult)body.eresult != EResult.OK)
                    {
                        continue;
                    }

                    if (resp.body.schema.Length < 1)
                    {
                        Console.WriteLine($"No schema returned for {steamId}");
                        continue;
                    }

                    var accountId = User.SteamID & 0xffffffff;
                    File.WriteAllBytes($"{StatsDir}/UserGameStatsSchema_{body.game_id}.bin", resp.body.schema);

                    var userStatsPath = $"{StatsDir}/UserGameStats_{accountId}_{body.game_id}.bin";
                    if (!File.Exists(userStatsPath))
                    {
                        File.WriteAllBytes(userStatsPath, new byte[]
                        {
                            0x00, 0x63, 0x61, 0x63, 0x68, 0x65, 0x00, 0x02, 0x63, 0x72, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x02, 0x50, 0x65, 0x6e, 0x64, 0x69, 0x6e, 0x67, 0x43, 0x68, 0x61, 0x6e, 0x67, 0x65, 0x73, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x08, 0x08
                        });
                    }
                    break;
                }
            }

            finished = true;
        }

        public void CallbackLoop()
        {
            while (!finished)
            {
                callbackManager.RunWaitCallbacks(TimeSpan.FromSeconds(1));
            }
        }

        public void Connect()
        {
            Client.Connect();
        }
    }

    static class Program
    {
        public static void Main(string[] args)
        {
            if (args.Length < 3 || (args.Length > 0 && args.Any(a => a == "-h" || a == "--help")))
            {
                Console.WriteLine("Usage: ./schema-grabber username password appId");
                Environment.Exit(1);
            }

            uint appId;
            if (!uint.TryParse(args[2], out appId))
            {
                Console.WriteLine($"{args[2]} is not a number!");
                Environment.Exit(1);
            }

            var grabber = new SchemaGrabber(args[0], args[1], appId);
            grabber.Connect();
            grabber.CallbackLoop();
        }
    }
}
