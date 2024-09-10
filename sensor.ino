#include <LiquidCrystal.h> // Incluye la librería para el LCD
#include <MKRWAN.h> // Include the MKRWAN.h library for LoRa communication

LiquidCrystal lcd(4, 5, 6, 7, 8, 9); // Inicializa el objeto LCD (rs, enable, d4, d5, d6, d7)
LoRaModem modem;

//Pines de control y lectura del sensor
byte sensorInterrupt = 0;
byte sensorPin = 1;

// Constantes
const char* sensorID = "YF-S402B"; // Identificador del sensor, cambiar a "EFS-04P" según el sensor utilizado
const String appEui = "0101010101010101";
const String appKey = "82690FF4C19BE4E962B23650B4DDE781";

volatile byte pulsosCont;
unsigned int flujoMililitros;
bool sesionIniciada = false;
int segundosSinFlujo = 0; // Para llevar el control de los 20 segundos sin flujo
float flujoRatio, litros, flujoMaximo, flujoMedio, sumaFlujo, calibracionFactor = 31.5;
unsigned long mililitrosTotal, inicioTiempoRiego, finTiempoRiego, tiempoSesion, auxTiempo, duracionRiego, tiempoConFlujo = 0;


void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2); 
  pinMode(sensorPin, INPUT_PULLUP); //Evitamos lecturas inestables del sensorPin
  attachInterrupt(digitalPinToInterrupt(sensorPin), pulsosConter, FALLING);
  resetVariables(); // Inicializamos las variables al inicio
  setupLoRa(); // Initialize LoRa connection
}

void loop() {
  unsigned long tiempoActual = millis();

  if ((tiempoActual - auxTiempo) > 1000) {
    detachInterrupt(sensorInterrupt);
    flujoRatio = ((1000.0 / (tiempoActual - auxTiempo)) * pulsosCont) / calibracionFactor;
    auxTiempo = tiempoActual;
    flujoMililitros = (flujoRatio / 60) * 1000;
    mililitrosTotal += flujoMililitros;

    // Solo iniciar la sesión si hay flujo
    if (!sesionIniciada && flujoRatio > 0) {
      sesionIniciada = true;
      inicioTiempoRiego = tiempoActual; // Registro del inicio del riego
    }

    // Contar segundos sin flujo
    if (flujoRatio == 0) {
      segundosSinFlujo++;
    } else {
      segundosSinFlujo = 0;
      tiempoConFlujo += 1000; // Acumular tiempo solo cuando hay flujo
    }

    // Si se detecta que han pasado 20 segundos sin flujo, terminar la sesión
    if (sesionIniciada && segundosSinFlujo >= 20) {
      finTiempoRiego = millis();
      duracionRiego = tiempoConFlujo / 1000;  // Duración efectiva solo con flujo
      flujoMedio = sumaFlujo / (tiempoConFlujo / 1000.0); // Promedio de flujo basado en el tiempo efectivo con flujo
      imprimirSesion(); // Imprimir los detalles de la sesión
      enviarDatosLoRa(); // Send data via LoRa when session ends
      resetVariables(); // Resetear las variables para la siguiente sesión
    }

    // Actualizar flujo máximo
    if (flujoRatio > flujoMaximo) {
      flujoMaximo = flujoRatio;
    }

    // Sumar flujo solo si hay flujo (excluyendo el flujo de 0 L/min)
    //evitamos interferencia de datos nulos en el procedimiento
    if (flujoRatio > 0) {
      sumaFlujo += flujoRatio;
    }

    //Imprimimos los datos en tiempo real
    imprimirDatosSerial_RT(); //Debug
    imprimirDatosLCD_RT();

    //Reiniciamos el contador de pulsos
    pulsosCont = 0;

    //Cada vez que detectamos flujo, activamos la interrupcion
    attachInterrupt(digitalPinToInterrupt(sensorInterrupt), pulsosConter, FALLING);
  }
}

// Función para el contador de pulsos
void pulsosConter() {
  pulsosCont++;
}

// Función para resetear las variables después de finalizar una sesión de riego
void resetVariables() {
  pulsosCont = 0;
  flujoRatio = 0.0;
  flujoMililitros = 0;
  mililitrosTotal = 0;
  sumaFlujo = 0;
  flujoMaximo = 0;
  flujoMedio = 0;
  inicioTiempoRiego = 0;
  finTiempoRiego = 0;
  sesionIniciada = false;
  segundosSinFlujo = 0;
  tiempoConFlujo = 0;
}

// Imprimir datos en puerto serie tiempo real
//Utilizado para Debug
void imprimirDatosSerial_RT(){
  Serial.print("Flow rate: ");
  Serial.print(flujoRatio);
  Serial.print(" L/min");
  Serial.print("\tTotal: ");
  Serial.print(mililitrosTotal);
  Serial.println(" mL");
}

// Imprimir datos en pantalla en tiempo real
void imprimirDatosLCD_RT(){
  lcd.setCursor(0, 0);  // Primera línea del LCD
  lcd.print("Rate: ");
  lcd.print(flujoRatio);
  lcd.print(" L/min");
  lcd.setCursor(0, 1);  // Segunda línea del LCD
  litros = mililitrosTotal / 1000.0;
  lcd.print("Vol: ");
  lcd.print(litros);
  lcd.print(" L");
}

// Función para imprimir los detalles de la sesión por el puerto serie
//Debug
void imprimirSesion() {
  Serial.println("Sesion de Riego Finalizada");
  Serial.print("Sensor: ");
  Serial.println(sensorID);
  Serial.print("Tiempo de riego: ");
  Serial.print(duracionRiego);
  Serial.println(" segundos");
  Serial.print("Flujo Maximo: ");
  Serial.print(flujoMaximo);
  Serial.println(" L/min");
  Serial.print("Flujo Medio: ");
  Serial.print(flujoMedio);
  Serial.println(" L/min");
  Serial.print("Volumen Total: ");
  Serial.print(litros);
  Serial.println(" L");
}

// Set up LoRa and establish connection via OTAA
void setupLoRa() {
  if (!modem.begin(EU868)) {
    Serial.println("Failed to start module");
    while (1) {}
  }

  Serial.print("Your module version is: ");
  Serial.println(modem.version());
  Serial.print("Your device EUI is: ");
  Serial.println(modem.deviceEUI());

  int connected = modem.joinOTAA(appEui, appKey);
  if (!connected) {
    Serial.println("Connection failed. Check your keys and try again.");
    while (1) {}
  } else {
    Serial.println("Successfully connected to TTN via OTAA.");
  }
}

// Send data via LoRa when the irrigation session ends
void enviarDatosLoRa() {
  modem.setPort(3);  // Set the LoRa port
  modem.beginPacket();

  // Enviar la cadena directamente
  modem.print(sensorID);

  // Convertir la duracion del riego y mandarla
  byte duration_payload[4];  // Array de bytes para almacenar el unsigned long
  // Convertir la duración en bytes (4 bytes en total)
  duration_payload[0] = (duracionRiego >> 24) & 0xFF;
  duration_payload[1] = (duracionRiego >> 16) & 0xFF;
  duration_payload[2] = (duracionRiego >> 8) & 0xFF;
  duration_payload[3] = duracionRiego & 0xFF;
  modem.write(duration_payload, sizeof(duration_payload));

  //Convertir flujoMaximo, flujoMedio, y litros totales
  //y mandarlos
  sendDataToLoRa(flujoMaximo);
  sendDataToLoRa(flujoMedio);
  sendDataToLoRa(litros);
  
  // Finalizar y enviar el mensaje
  int err = modem.endPacket(true);
  if (err > 0) {
    Serial.println("Datos de riego enviados correctamente a TTN.");
  } else {
    Serial.println("Error al enviar los datos.");
  }
}

// Función genérica para enviar datos (unsigned long o float)
template <typename T>
void sendDataToLoRa(T data) {
  byte buffer[sizeof(T)];
  memcpy(buffer, &data, sizeof(T));
  modem.write(buffer, sizeof(T));
}