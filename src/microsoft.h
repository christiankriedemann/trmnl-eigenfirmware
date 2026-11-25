#pragma once
#include <Arduino.h>

#include "data.h"

// Microsoft 365 ueber Microsoft Graph, delegiert (Anmeldung per Geraetecode).
// Einstellungen: ms_client (Anwendungs-ID), ms_tenant (Verzeichnis-ID oder "organizations"),
// ms_refresh (Refresh-Token, geheim), ms_user (angemeldetes Konto, nur Anzeige),
// ms_cals (Kalender-IDs, leer = Standardkalender), ms_lists (To-Do-Listen-IDs, leer = alle
// ausser "Gekennzeichnete E-Mails"), ms_planner (1 = Planner-Aufgaben, Standard 1).
namespace ms {

bool configured();  // Client-ID gesetzt
bool signedIn();    // Refresh-Token vorhanden

struct DeviceLogin {
  String userCode, verificationUri, deviceCode, message;
  int interval = 5, expiresIn = 900;
};

// Schritt 1: Code anfordern
bool startLogin(DeviceLogin &dl, String &err);
// Schritt 2: warten, bis der Nutzer bestaetigt hat (blockiert bis Erfolg, Ablauf oder Abbruch)
bool finishLogin(const DeviceLogin &dl, String &err, bool (*cancel)() = nullptr);

bool calendar(RawData &raw, time_t from, time_t to, String &err);
bool tasks(RawData &raw, String &err);

bool calendarList(String &jsonOut, String &err);
bool todoLists(String &jsonOut, String &err);

}  // namespace ms
