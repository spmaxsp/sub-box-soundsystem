#define MAIN_R  12
#define NTC_BP  13  
#define IN      3
#define OUT     2

unsigned long start_timestamp = 0;
unsigned long now_timestamp = 0;
unsigned long delay_1 = 1 * 1000;
unsigned long delay_2 = 3 * 1000;

void setup() {
  pinMode(MAIN_R, OUTPUT);
  pinMode(NTC_BP, OUTPUT);
  pinMode(IN, INPUT);
  pinMode(OUT, OUTPUT);

  digitalWrite(MAIN_R, LOW);
  digitalWrite(NTC_BP, LOW);
  digitalWrite(OUT, LOW);
}

void loop() {
  if(!digitalRead(IN)){
    start_timestamp = millis();
  }
  now_timestamp = millis();

  if(now_timestamp - start_timestamp > delay_1 && now_timestamp - start_timestamp < delay_2){
    digitalWrite(MAIN_R, HIGH);
    digitalWrite(NTC_BP, LOW);
    digitalWrite(OUT, LOW);
  }
  else if(now_timestamp - start_timestamp > delay_2){
    digitalWrite(MAIN_R, HIGH);
    digitalWrite(NTC_BP, HIGH);
    digitalWrite(OUT, HIGH);
  }
  else {
    digitalWrite(MAIN_R, LOW);
     digitalWrite(NTC_BP, LOW);
    digitalWrite(OUT, LOW);
  }

}
