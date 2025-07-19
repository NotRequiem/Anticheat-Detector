#pragma once

#include <cstdint>
#include <string>
#include <deque>
#include <algorithm> 

enum class Anticheat {
    UNKNOWN, // assigned when we don't know the current anticheat yet or when we want to start infering the anticheat again in a new session
    POLAR,
    INTAVE,
    VERUS,
    VULCAN,
    KARHU,
    GRIM,
    AGC,
    WATCHDOG,
    NCP, // meme
    CONFIRMED, // basically marks the case as closed (we detected an anticheat)
    ANALYSIS_FAILED // if we know that theres an anticheat but we dont know which (maybe private or just a dogwata anticheeto)
};

class TransactionAnalyzer {
public:
    TransactionAnalyzer();
    // Analyzes incoming server->client transaction packets
    void analyzeServer(int16_t id);
    // Analyzes outgoing client->server transaction packets
    void analyzeClient(int16_t id);

private:
    void notify_detection(const std::string& ac_name);

    // This stores the last anticheat we successfully identified and reported
    Anticheat lastNotifiedAnticheat;

    // This tracks our best guess for the current minecraft server before it's confirmed
    Anticheat currentGuess;

    // A queue of transaction IDs received from the server, awaiting a client response
    std::deque<int16_t> server_transaction_queue;

    int16_t lastId;
    int packetCount;
};