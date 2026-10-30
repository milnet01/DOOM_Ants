// d_ledger.h — DOOM-0502: the engine's side of a frameledger benchmark run.
#ifndef D_LEDGER_H
#define D_LEDGER_H

// Reads the flags and the machine's load. Call first thing, before the engine
// loads anything.
void   D_LedgerInit (void);

// Top of the main loop. Quits the program when the frame limit is reached.
void   D_LedgerFrameBegin (void);

// After this frame's tics ran: how many ran, and how long the game took.
void   D_LedgerAfterTics (int tics, double tic_ms);

// The ledger's clock; 0 when no run was asked for, so a plain launch pays nothing.
double D_LedgerClockMs (void);

// Writes the close line. I_QuitTeardown calls it, so a player's quit finishes the
// file too. A crash leaves no close line, which is how the analyser knows.
void   D_LedgerFinish (void);

#endif
