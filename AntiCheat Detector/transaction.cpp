#include "transaction.hpp"
#include "console.hpp"
#include "shared.hh"

const int MAX_ANALYSIS_PACKETS = 15; // should be more than enough, otherwise it would spam the console of the user with tons of shit

TransactionAnalyzer::TransactionAnalyzer() :
    lastNotifiedAnticheat(Anticheat::UNKNOWN),
    currentGuess(Anticheat::UNKNOWN),
    lastId(0),
    packetCount(0) {
}

// self-explanatory
static std::string getAnticheatName(Anticheat ac) {
    switch (ac) {
    case Anticheat::POLAR:    return "Polar"; // we all love Luckyyyyeee i wont try to detect his anticheat :)
    case Anticheat::INTAVE:   return "Intave"; // idk about lennox but he seems cool, the detection for his anticheat was made from old augustus wiki so its prob not updated lol
    case Anticheat::VERUS:    return "Verus"; // absolutely not sorry
    case Anticheat::VULCAN:   return "Vulcan"; // sorry frap
    case Anticheat::KARHU:    return "Karhu"; // sorry Johannes 
    case Anticheat::GRIM:     return "Grim"; // sorry MWHunter
    case Anticheat::AGC:      return "AntiGamingChair"; // found in mmc, but sometimes people used to use older versions in other servers like InvadedLands
    case Anticheat::WATCHDOG: return "Watchdog"; // this is obvious, will only be in hypixel, just for reference
    case Anticheat::NCP:      return "UpdatedNCP"; // IMAGINE TRYING TO DETECT NoCheatPlus WITH TRANSACTIONS LOLOL
    default:                  return "Unknown";
    }
}

// This function now writes the detected AC name to shared memory
// and then signals the event
void TransactionAnalyzer::notify_detection(const std::string& ac_name) {
    // check if the global handles from InitThread are valid
    if (g_hIpcMapFile == NULL || g_hIpcEvent == NULL) {
        Log(FATAL, "IPC handles are not valid. Cannot send notification.");
        return;
    }

    // get a pointer to the already-created shared memory, or at least it should be.
    LPVOID pBuf = MapViewOfFile(g_hIpcMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEM_SIZE);
    if (pBuf == NULL) {
        Log(FATAL, "Could not map view of file (%d).", GetLastError());
        return;
    }

    // write the anticheat name into the shared memory
    strcpy_s(static_cast<char*>(pBuf), SHARED_MEM_SIZE, ac_name.c_str());
    UnmapViewOfFile(pBuf);

    // signal the already-created event
    if (SetEvent(g_hIpcEvent)) {
        Log(INFO, "Successfully signaled detection event for %s.", ac_name.c_str());
    }
    else {
        Log(FATAL, "Failed to set IPC event (%d).", GetLastError());
    }
}

// The main event. The Sherlock Holmes (not the one from redlotus) of transaction packets 
// This function handles S32PacketConfirmTransaction (SERVER -> CLIENT)
void TransactionAnalyzer::analyzeServer(int16_t id) {
    Log(INFO, "[S->C] Received transaction: %d", id);
    server_transaction_queue.push_back(id);
    if (server_transaction_queue.size() > 100) {
        server_transaction_queue.pop_front();
    }

    // On the very first packet of a session, try to make an initial guess based on specific IDs.
    if (id == -32768)      currentGuess = Anticheat::INTAVE;
    else if (id == -32767) currentGuess = Anticheat::KARHU;
    else if (id == -30767) currentGuess = Anticheat::VULCAN; // same for Negativity Anticheat
    else if (id == 0)      currentGuess = Anticheat::GRIM;
    else if (id == 1)      currentGuess = Anticheat::AGC;

    if (currentGuess != Anticheat::UNKNOWN) {
        Log(SUCCESS, "Initial guess: %s", getAnticheatName(currentGuess).c_str());
    }    

    packetCount++;
    bool confirmed = false;

    // If we have an initial guess from the first packet, try to confirm it, althought some anticheats are so easy to detect that don't need extra verification (we just confirm that they sent another transaction)
    if (currentGuess != Anticheat::UNKNOWN && packetCount > 1) {
        switch (currentGuess) {
        case Anticheat::GRIM:
            if (id == -1)
                confirmed = true;
            break;
        case Anticheat::AGC:
            if (id == 2)
                confirmed = true;
            break;
        case Anticheat::KARHU:
            if ((lastId == -32767 && id == -3000) || (lastId <= -3000 && id == lastId - 1)) 
                confirmed = true;
            break;
        case Anticheat::INTAVE:
            if (id == lastId + 1)
                confirmed = true;
            break;
        case Anticheat::VULCAN: 
            if (lastId != -30768 && lastId != -30766) // basically what this tries to prevent is that other anticheat, after a lot of time (probably hours), reach this transaction number, false flagging another anticheat
                confirmed = true; // Vulcan increments by 5 but it always starts at the same id, so there's no need for extra checks
            break;
        default: break;
        }
    }
    // if initial guess failed, try to find a pattern based on sequential IDs
    else if (currentGuess == Anticheat::UNKNOWN && packetCount > 1) {
        // Verus often uses a random starting ID and then increments by 1
        if (id == lastId + 1 && lastNotifiedAnticheat != Anticheat::VULCAN) {
            currentGuess = Anticheat::VERUS;
            confirmed = true;
        }
    }

    if (confirmed) {
        if (currentGuess != lastNotifiedAnticheat) {
            std::string acName = getAnticheatName(currentGuess);
            Log(SUCCESS, "Pattern confirmed! New anticheat detected: %s", acName.c_str());
            notify_detection(acName);
            lastNotifiedAnticheat = currentGuess;
        }
        // After confirmation, reset to be ready for the next server change
        currentGuess = Anticheat::UNKNOWN;
        packetCount = 0; // Reset packet count to restart analysis on the next packet
    }

    lastId = id;
}

// Function to handle C0FPacketConfirmTransaction (CLIENT -> SERVER)
void TransactionAnalyzer::analyzeClient(int16_t id) {
    Log(INFO, "[C->S] Sent transaction: %d", id);

    // Check if the client is responding to a known transaction
    auto it = std::find(server_transaction_queue.begin(), server_transaction_queue.end(), id);
    if (it != server_transaction_queue.end()) {
        Log(SUCCESS, "Client responded to server transaction %d correctly.", id);
        server_transaction_queue.erase(it);
    }
    else {
        Log(DETAIL, "Client sent unexpected transaction %d, was not in server queue.", id);
    }
}