#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Arduino_JSON.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// Variabili di rete e NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);  // fuso orario +1 (3600 secondi), aggiornamento ogni minuto

// Definizione dei pin per il modulo TFT LCD
#define TFT_CS 15
#define TFT_RST 4
#define TFT_DC 2
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_MISO 19

// Inizializza il display ILI9341
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST, TFT_MISO);


// Dati della rete WiFi
#ifndef STASSID
#define STASSID "your wifi name here"
#define STAPSK "your password here"
#endif
const char* ssid = STASSID;
const char* password = STAPSK;

// URL dell'API Shelly EM
const char* shellyapiurl = "http://192.168.1.113/status";

// Parametri di configurazione
#define NCYCLE 30    // Secondi tra le rilevazioni potenza
#define MAXPWP 3000  // Potenza massima del contatore
#define MAXFVP 600   // Potenza massima fotovoltaico

int cycle = 0;
int overpower = 0;
double fv_negative_sum = 0;                   // Accumulo dei valori negativi di FV
double rete_positive_sum = 0;                 // Accumulo dei valori positivi di potenza assorbita dalla rete
unsigned long lastUpdate = 0;                 // Tempo dell'ultimo aggiornamento
double max_fv = 0;                            // Massimo assoluto FV
double max_rete = 0;                          // Massimo assoluto rete
unsigned long wifiConnectTimeout = millis();  // Tempo di inizio
unsigned long timeout = 10000;                // 10 secondi di timeout

// Definizione delle variabili per i massimi giornalieri
double max_fv_giornaliero = 0;
double max_rete_giornaliero = 0;

double dp0 = 0, dp1 = 0;  // Dati di potenza
// Dichiarazione delle variabili globali per mantenere il totale della potenza
double total_p0 = 0;  // Totale generato dal fotovoltaico (kWh)
double total_p1 = 0;  // Totale della rete (kWh)

// Aggiungi una condizione per azzerare i contatori alla fine del giorno
unsigned long lastMidnightReset = 0;  // Timestamp per il reset a mezzanotte

// Dichiarazione delle funzioni
void drawScreen(float fvP, float pwP, float total_p0, float total_p1, String timeStr);
void drawProgressBar(int x, int y, int width, int height, float percentage, uint16_t color);
void drawAlert(int c);


// Funzione globale che recupera i dati da Shelly
String getShellyData() {
  String ret;
  WiFiClient wifiClient;
  HTTPClient http;
  http.begin(wifiClient, shellyapiurl);
  int statusCode = http.GET();
  ret = http.getString();
  http.end();

  // Parsing JSON per estrarre l'ora
  JSONVar data = JSON.parse(ret);
  if (JSON.typeof(data) == "undefined") {
    Serial.println("Errore nel parsing dei dati");
    return "";
  }

  String timeStr = (const char*)data["time"];    // Es. "09:22"
  int hour = timeStr.substring(0, 2).toInt();    // Estrai ore
  int minute = timeStr.substring(3, 5).toInt();  // Estrai minuti

  Serial.print("Ora: ");
  Serial.print(hour);
  Serial.print(":");
  Serial.println(minute);

  return ret;
}

void setup() {
  Serial.begin(9600);  //115200
  Serial.println("Inizializzazione...");

  // Inizializzazione del display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Init WiFi...");
  Serial.println("Connessione a WiFi...");

  // Configurazione della connessione WiFi
  WiFi.begin(ssid, password);
  tft.println("Connessione WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnesso a WiFi!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("Connessione a WiFi...");

  // Mostra l'IP sul display
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.println("Connesso!");
  tft.println("IP:");
  tft.println(WiFi.localIP().toString());

  // Avvio del client NTP
  timeClient.begin();
  timeClient.update();

  lastMidnightReset = millis();  // inizializzazione reset
}

// Funzione per azzerare i massimi giornalieri a mezzanotte
void resetMassimiGiornalieri() {
  max_fv_giornaliero = 0;
  max_rete_giornaliero = 0;
}

void loop() {
  timeClient.update();  // Aggiorna l'ora del client NTP

  // Controlla se è mezzanotte per azzerare i contatori e i massimi giornalieri
  if (timeClient.getHours() == 0 && timeClient.getMinutes() == 0 && millis() - lastMidnightReset > 60000) {
    resetMassimiGiornalieri();
    fv_negative_sum = 0;
    rete_positive_sum = 0;
    lastMidnightReset = millis();
  }



  // Calcolo dei valori ogni 30 secondi
  if (millis() - lastUpdate >= NCYCLE * 1000) {  // 30000 millisecondi
    lastUpdate = millis();                       // Aggiorna il tempo dell'ultimo aggiornamento
    String js = getShellyData();  // Recupera i dati dall'API Shelly
    JSONVar data = JSON.parse(js);

    if (JSON.typeof(data) != "undefined") {
      String p0 = JSON.stringify(data["emeters"][1]["power"]);  // Potenza FV
      String p1 = JSON.stringify(data["emeters"][0]["power"]);  // Potenza rete
      String timeStr = JSON.stringify(data["time"]);            // Ora letta da Shelly

      if (p1 != "null") {
        dp0 = p0.toDouble();  // Converte il valore di potenza FV
        dp1 = p1.toDouble();  // Converte il valore di potenza rete

        // Aggiorna i massimi giornalieri
        if (abs(dp0) > max_fv_giornaliero) {
          max_fv_giornaliero = abs(dp0);
        }
        if (dp1 > max_rete_giornaliero) {
          max_rete_giornaliero = dp1;
        }
        // Aggiorna i massimi assoluti
        if (abs(dp0) > max_fv) {
          max_fv = abs(dp0);  // Imposta il massimo assoluto per il FV
        }
        if (dp1 > max_rete) {
          max_rete = dp1;  // Imposta il massimo per la rete
        }
        // Estrai ore e minuti dalla stringa di ora
        int hour = timeStr.substring(1, 3).toInt();
        int minute = timeStr.substring(4, 6).toInt();

        // Accumula i valori negativi di FV tra le 7 e le 21
        if (hour >= 7 && hour < 21 && dp0 < 0) {
          fv_negative_sum += dp0;
        }

        // Accumula i valori positivi della rete per tutto il giorno
        if (dp1 > 0) {
          rete_positive_sum += dp1;
        }

        // Calcola il totale in kWh per la visualizzazione
        total_p0 = fv_negative_sum / 120.0;    // kWh generati da FV in un ora con misura ogni 30 sec
        total_p1 = rete_positive_sum / 120.0;  // kWh assorbiti dalla rete ""

        drawScreen(dp0, dp1, total_p0, total_p1, timeStr);  // Aggiorna il display
      } else {
        tft.fillScreen(ILI9341_BLACK);
        tft.setCursor(0, 0);
        tft.setTextSize(2);
        tft.println("Errore:");
        tft.println("ShellyEM non raggiungibile");
        delay(10000);
      }
    }
  }

  // Gestisce l'alert di overpower
  if (dp1 > MAXPWP) {
    drawAlert(overpower);
    overpower++;
  } else {
    overpower = 0;
  }

  delay(1000);  // Delay per evitare letture troppo frequenti
}


void drawScreen(float fvP, float pwP, float total_p0, float total_p1, String timeStr) {
  char buffer[64];

  // Cambia lo sfondo in rosso se pwP è negativo, altrimenti rimane nero
  tft.fillScreen(pwP < 0 ? ILI9341_RED : ILI9341_BLACK);

  // Stampa titolo sulla prima riga
  tft.setCursor(0, 00);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);  // Cambia il colore in bianco
  tft.println("   -- Power Monitor --");

  // Stampa l'ora in alto a destra con carattere di altezza 1
  tft.setCursor(270, 10);           // Posiziona in alto a destra
  tft.setTextSize(1);               // Imposta l'altezza del carattere a 1
  tft.setTextColor(ILI9341_WHITE);  // Colore del testo
  tft.print(timeStr);               // Mostra l'ora

  // Stampa FV
  sprintf(buffer, " FV: %.0fW", fvP);
  tft.setCursor(0, 40);
  tft.setTextSize(2);
  tft.setTextColor(fvP >= 0 ? ILI9341_ORANGE : ILI9341_GREEN);
  tft.println(buffer);

  // Visualizza il massimo giornaliero di FV
  sprintf(buffer, "Day Max:%.0fW", max_fv_giornaliero);
  tft.setCursor(160, 40);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREEN);
  tft.println(buffer);

  // Stampa il massimo assoluto di FV
  sprintf(buffer, "Max:%.0fW", max_fv);
  tft.setCursor(160, 50);  // Aggiungi una riga sotto i totali
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREEN);
  tft.println(buffer);

  // Calcola la percentuale per FV, mappando su un intervallo da -100% a +100%
  float progressPercentagePV = (fvP / MAXFVP) * 100;
  drawProgressBar(0, 60, 320, 20, progressPercentagePV, fvP >= 0 ? ILI9341_ORANGE : ILI9341_GREEN);

  // Mostra la percentuale della barra FV
  tft.setCursor(270, 40);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN);
  tft.print((int)progressPercentagePV);
  tft.println("%");

  // Stampa Rete
  sprintf(buffer, " Rete: %.0fW", pwP);  ///   sprintf(buffer, " Rete: %.0fW", pwP);
  tft.setCursor(0, 140);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  tft.println(buffer);

  // Stampa il massimo assoluto della rete
  sprintf(buffer, "Max:%.0fW", max_rete);
  tft.setCursor(160, 150);  // Aggiungi una riga sotto
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.println(buffer);

  // Visualizza il massimo giornaliero e assoluto di Rete
  sprintf(buffer, "Day Max:%.0fW", max_rete_giornaliero);
  tft.setCursor(160, 140);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.println(buffer);

  // Calcola la percentuale per Rete, mappando su un intervallo da -100% a +100%
  float progressPercentagePW = (pwP / MAXPWP) * 100;
  drawProgressBar(0, 160, 320, 20, progressPercentagePW, ILI9341_YELLOW);

  // Mostra la percentuale della barra Rete
  tft.setCursor(270, 140);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  tft.print((int)progressPercentagePW);
  tft.println("%");

  // Stampa Totale FV restituito giornaliero
  sprintf(buffer, " Day gen FV: %.4fkWh", total_p0 / 1000);  //  sprintf(buffer, " Day gen FV: %.1fkWh", total_p0 / 1000);
  tft.setCursor(0, 90);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.println(buffer);

  // Stampa Totale Rete restituito giornaliero

  sprintf(buffer, " Day ass Rete: %.4fkWh", total_p1 / 1000);
  tft.setCursor(0, 190);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN);
  tft.println(buffer);



  if (pwP > MAXPWP) {
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(0, 220);
    tft.setTextSize(2);
    tft.println("** OVER POWER **");
  } else {
    // Puoi anche pulire solo la riga dove c'era l'errore
    tft.fillRect(0, 220, 320, 30, ILI9341_BLACK);  // Pulisci la riga dell'overpower
  }


  // Aggiungi la frase sull'ultima riga
  tft.setCursor(0, 230);  // Posizione nella parte inferiore dello schermo
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.println("Val Negativi: kW erogati; Val Positivi: kW assorbiti");
}


void drawProgressBar(int x, int y, int width, int height, float percentage, uint16_t color) {
  percentage = min(max(percentage, -100.0f), 100.0f);  // Limita il valore tra -100 e 100
  int halfWidth = width / 2;                           // Calcola la metà della larghezza per il punto centrale

  // Cancella l'area della barra di progresso
  tft.drawRect(x, y, width, height, ILI9341_WHITE);                  // Cornice della barra
  tft.fillRect(x + 1, y + 1, width - 2, height - 2, ILI9341_BLACK);  // Svuota la barra

  if (percentage > 0) {
    // Se il valore è positivo, disegna la barra verso destra
    int filledWidth = (int)(halfWidth * (percentage / 100.0f));
    tft.fillRect(x + halfWidth, y + 1, filledWidth, height - 2, color);
  } else if (percentage < 0) {
    // Se il valore è negativo, disegna la barra verso sinistra
    int filledWidth = (int)(halfWidth * (-percentage / 100.0f));
    tft.fillRect(x + halfWidth - filledWidth, y + 1, filledWidth, height - 2, color);
  }
}

void drawAlert(int c) {
  tft.fillScreen(ILI9341_BLACK);
  if ((c % 2) == 1) {
    tft.setCursor(0, 50);
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_RED);
    tft.println("OVERPOWER!");
  }
}

//////////////modificare da qui/////////////////////////////////////////
