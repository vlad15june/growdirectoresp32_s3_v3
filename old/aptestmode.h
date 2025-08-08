/*

This code performs testing via the serial port of all functional cascades and nodes of the device,
 interacting with a JIG fixture based on configuration specific to a particular device.

*/

char commandBuffer[150];
int indBuf = 0;

void TEST_configured()
{
  char ch = Serial.read();
  if (ch == '#')
  {
    commandBuffer[0] = ch;
    indBuf = 1;
    return;
  }
  if (indBuf == 0 || indBuf >= 150)
  {
    indBuf = 0;
    return;
  }
  if (ch != '%')
  {
    commandBuffer[indBuf] = ch;
    indBuf++;
    return;
  }

  commandBuffer[indBuf] = 0;
  indBuf = 0;

  String str(commandBuffer);

  // 0.    #restart%
  if (str.startsWith("#restart") == true)
  {
    Serial.println("Rebooting.......");
    delay(1000);
    ESP.restart();
  }

  // 1.    #set_ref_voltage -ch 0%     TS0=1
  if (str.startsWith("#set_ref_voltage") == true)
  {
    uint8_t channel = (str.substring(21)).toInt();
    digitalWrite(pin_TST_SNS[channel], LOW);
    delay(300); // neutralize transients
    Serial.printf("TS%d=%d\n", channel, digitalRead(pin_TST_SNS[channel]));
    return;
  }

  // 2.    #clr_ref_voltage -ch 0%
  if (str.startsWith("#clr_ref_voltage") == true)
  {
    uint8_t channel = (str.substring(21)).toInt();
    digitalWrite(pin_TST_SNS[channel], HIGH);
    delay(300); // neutralize transients
    Serial.printf("TS%d=%d\n", channel, digitalRead(pin_TST_SNS[channel]));
    return;
  }

  // 3.    #read_adc -ch 0%
  if (str.startsWith("#read_adc") == true)
  {
    uint8_t channel = (str.substring(14)).toInt();
    Serial.printf("ch%d=%d\n", channel, analogRead(adcPin[channel]));
    return;
  }

  // 4.    #read_version%
  if (str.startsWith("#read_version") == true)
  {
    Serial.println(firmwareVersion);
    return;
  }

  // 5.    #read_i2c -addr 56%
  if (str.startsWith("#read_i2c") == true)
  {
    int addr = (str.substring(16)).toInt();
    Wire.beginTransmission(addr);
    int error = Wire.endTransmission();
    if (error != 0)
    {
      Serial.printf("read_i2c_error %d\n", error);
    }
    else
    {
      Serial.printf("addr = %d is ok\n", addr);
    }
    return;
  }

  // 5.1    #scan_i2c%
  if (str.startsWith("#scan_i2c") == true)
  {
    int addr = 0;
    bool isFound = false;
    while (/*isFound == false && */ addr < 256)
    {
      Wire.beginTransmission(addr);
      int error = Wire.endTransmission();
      delay(50);
      if (error != 0)
      {
        Serial.printf("addr = %d is read_i2c_error %d\n", addr, error);
      }
      else
      {
        Serial.printf("addr = %d is ok\n", addr);
        isFound = true;
      }
      addr++;
    }
    return;
  }

  // 5.2    #Tibbit_read_i2c%
  if (str.startsWith("#Tibbit_read_i2c") == true)
  {
    int addr = 35;

    // Wire.beginTransmission(addr);
    // Wire.write(0x01);
    // int error = Wire.endTransmission();
    // delay(50);
    // if (error != 0)
    // {
    //   Serial.printf("addr = %d is read_i2c_error %d\n", addr, error);
    // }
    // else
    // {
    //   Serial.printf("power on ok\n", addr);
    // }

    Wire.beginTransmission(addr);
    Wire.write(0x10);
    int error = Wire.endTransmission();
    delay(50);
    if (error != 0)
    {
      Serial.printf("addr = %d is read_i2c_error %d\n", addr, error);
      return;
    }
    else
    {
      Serial.printf("A-Resolution mode ok\n", addr);
    }

    if (Wire.requestFrom(addr, 2) != 2)
    {
      Serial.printf("read data error\n", addr, error);
    }
    else
    {
      byte msb = Wire.read();
      byte lsb = Wire.read();
      Serial.printf("msb = %d lsb = %d\n", msb, lsb);
    }

    return;
  }

  // 6.    #write_i2c -addr 112 -v 1%     (v= 1, 2, 4 or 8)
  if (str.startsWith("#write_i2c") == true)
  {
    String addr_str = str.substring(17, 20);
    addr_str.trim();
    int addr = addr_str.toInt();
    Wire.beginTransmission(addr);
    String val_str = str.substring(23);
    val_str.trim();
    byte val = val_str.toInt();
    Wire.write(val); // sends value
    int error = Wire.endTransmission();
    if (error != 0)
    {
      Serial.printf("write_i2c_error %d\n", error);
    }
    if (error == 0)
    {
      Serial.printf("ok\n");
    }
    return;
  }

  // 7.    #read_mac_address%
  if (str.startsWith("#read_mac_address") == true)
  {
    String macAddr = WiFi.macAddress();
    if (macAddr.length() > 0)
    {
      Serial.println(macAddr);
    }
    else
    {
      Serial.println("read_mac_address_error");
    }
    return;
  }

  /*
  //8.    #read_pg%
  if (str.startsWith("#read_pg")) {
    char response[32] = "";
    uint16_t responseType = readPowerGood(response);
    Serial.printf("%s", response);
    return;
  }
  */

  // 8.1.    #read_pg_3_3%
  if (str.startsWith("#read_pg_3_3"))
  {
    char response[32] = "";
    uint16_t responseType = readPowerGood33(response);
    Serial.printf("%s", response);
    return;
  }

  // 8.2.    #read_pg_5_0%
  if (str.startsWith("#read_pg_5_0"))
  {
    char response[32] = "";
    uint16_t responseType = readPowerGood50(response);
    Serial.printf("%s", response);
    return;
  }

  // 8.3.    #read_pg_5_5%
  if (str.startsWith("#read_pg_5_5"))
  {
    char response[32] = "";
    uint16_t responseType = readPowerGood55(response);
    Serial.printf("%s", response);
    return;
  }

  // 9.    #ps_en -ch 0 -v 0%     ch=0..3 channel,  v=0/1 off/on    other  #ps_en*** read PS status all channels
  if (str.startsWith("#ps_en"))
  {
    switch (str.length())
    {
    // Activate/Deactivate PS on channel ch
    case 17:
    {
      uint8_t channel = (str.substring(11)).toInt();
      uint8_t value = (str.substring(16)).toInt();
      if (value != 1)
        value = 0;
      digitalWrite(pin_SENSOR_PS[channel], value);
      Serial.printf("PS%d=%d\n", channel, digitalRead(pin_SENSOR_PS[channel]));
      break;
    }
    // Read PS status on channel ch
    case 12:
    {
      uint8_t channel = (str.substring(11)).toInt();
      Serial.printf("PS%d=%d\n", channel, digitalRead(pin_SENSOR_PS[channel]));
      break;
    }
    }
    return;
  }

  // 10.    #read_wifi_rssi%
  if (str.startsWith("#read_wifi_rssi") == true)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println(WiFi.RSSI());
    }
    else
    {
      Serial.printf("read_wifi_rssi_error %d\n", WiFi.status());
    }
    return;
  }

  // 11.    #connect_to_wifi -u SSID -p PASS%
  if (str.startsWith("#connect_to_wifi") == true)
  {
    String ssidTest;
    String passwordTest;

    str += " ";
    int pos = 0;
    int prevPos = 0;
    String token;
    char delimiter = ' ';
    pos = str.indexOf(delimiter, prevPos);
    while (pos != -1)
    {
      token = str.substring(prevPos, pos);
      prevPos = pos + 1;
      if (token == "-u")
      {
        pos = str.indexOf(delimiter, prevPos);
        if (pos != -1)
        {
          ssidTest = str.substring(prevPos, pos);
          prevPos = pos + 1;
        }
      }
      if (token == "-p")
      {
        pos = str.indexOf(delimiter, prevPos);
        if (pos != -1)
        {
          passwordTest = str.substring(prevPos, pos);
          prevPos = pos + 1;
        }
      }
      pos = str.indexOf(delimiter, prevPos);
    }

    WiFi.begin(ssidTest.c_str(), passwordTest.c_str());
    byte tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 40)
    {
      delay(200);
      tries++;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("ok");
    }
    else
    {
      Serial.println("read_wifi_rssi_error");
    }
    return;
  }

  // 12.    #clear_all_file_log%
  if (str.startsWith("#clear_all_file_log") == true)
  {
    clearAllFileLog();
    return;
  }

  // 13.    #write_diagnostic%
  if (str.startsWith("#write_diagnostic") == true)
  {
    writeDiagnosticToMqtt();
    return;
  }

  // 14.    #read_startup_log%
  if (str.startsWith("#read_startup_log") == true)
  {
    readStartupLogToMqtt();
    return;
  }

  // 15.    #set_test_success%
  if (str.startsWith("#set_test_success") == true)
  {
    putIfConfigured(IS_USER_CAN_CONFIGURE);
    isConfigured = IS_CONFIGURED_NEED_RESTART;
    Serial.println("ok");
    return;
  }

  // 16.   #set_to_test%
  if (str.startsWith("#set_to_test") == true)
  {
    Serial.println("set_to_test");
    putIfConfigured(IS_AP_TEST_MODE);
    isConfigured = IS_CONFIGURED_NEED_RESTART;
    return;
  }

  // 17.   #read_relay -ch 0%            ch0=1         {ch0=0;ch1=0;ch2=1;ch3=1}
  if (str.startsWith("#read_relay") == true)
  {
    uint8_t channel = (str.substring(16)).toInt();
    Serial.printf("ch%d=%d\n", channel, digitalRead(pin[channel]));
    return;
  }

  // 18.   #set_relay -ch 0 -v 1%
  if (str.startsWith("#set_relay") == true)
  {
    uint8_t channel = (str.substring(15)).toInt();
    uint8_t value = (str.substring(20)).toInt();
    if (value != 1)
      value = 0;
    digitalWrite(pin[channel], value);
    Serial.printf("ch%d=%d\n", channel, digitalRead(pin[channel]));
    return;
  }

  // 19.   #led_r -ch 0 -v 1%
  if (str.startsWith("#led_r"))
  {
    uint8_t ledChannel = (str.substring(11, 12)).toInt();
    uint8_t ledValue = (str.substring(16)).toInt();
    char answer[10];
    ledsTesting(answer, LEDRED, ledChannel, ledValue);
    Serial.println(answer);
    return;
  }

  // 20.   #led_g -ch 0 -v 1%
  if (str.startsWith("#led_g"))
  {
    uint8_t ledChannel = (str.substring(11, 12)).toInt();
    uint8_t ledValue = (str.substring(16)).toInt();
    char answer[10];
    ledsTesting(answer, LEDGREEN, ledChannel, ledValue);
    Serial.println(answer);
    return;
  }

  // 21. #read_pump -ch 0%      ch0=0    {ch0=0;ch1=1;ch2=0}
  if (str.startsWith("#read_pump"))
  {
    uint8_t channel = (str.substring(15)).toInt();
    pinMode(PUMP_HYDRO[channel], OUTPUT);
    Serial.printf("ch%d=%d\n", channel, digitalRead(PUMP_HYDRO[channel]));
    return;
  }

  // 22. #set_pump -ch 0 -v 1%      ch0=1
  if (str.startsWith("#set_pump"))
  {
    uint8_t channel = (str.substring(14)).toInt();
    uint8_t value = (str.substring(19)).toInt();
    if (value != 1)
      value = 0;
    pinMode(PUMP_HYDRO[channel], OUTPUT);
    digitalWrite(PUMP_HYDRO[channel], value);
    Serial.printf("ch%d=%d\n", channel, digitalRead(PUMP_HYDRO[channel]));
    return;
  }

  // 23. #do_orp -v 1%         v=0/1   0=DO  1=ORP      #do_orp% - read current state (test & data ch=3)
  if (str.startsWith("#do_orp"))
  {
    // If there is no parameter v, the current state is output.
    if (str.length() == 12)
    {
      uint8_t value = (str.substring(11)).toInt();
      if (value != 1)
        value = 0;
      pinMode(DO_ORP, OUTPUT);
      digitalWrite(DO_ORP, value);
    }
    if (digitalRead(DO_ORP) == 0)
      Serial.println("{DO active}");
    if (digitalRead(DO_ORP) == 1)
      Serial.println("{ORP active}");
    return;
  }

  // 24. #ec_sdn -v 1%         v=0/1  off/on   |    #ec_sdn% - read current state   LOW=on  HIGH=off      #ec_sdn% - read current state (test & data ch=1)
  if (str.startsWith("#ec_sdn"))
  {
    // If there is no parameter v, the current state is output.
    if (str.length() == 12)
    {
      uint8_t value = (str.substring(11)).toInt();
      if (value != 1)
        value = 0;
      value++; // 0->1 | 1->2
      if (value != 1)
        value = 0; // 1->1 | 2->0    EC On LOW Level
      pinMode(EC_SDN, OUTPUT);
      digitalWrite(EC_SDN, value);
    }
    if (digitalRead(EC_SDN) == 0)
      Serial.println("{EC On}");
    if (digitalRead(EC_SDN) == 1)
      Serial.println("{EC Off}");
    return;
  }

  // 25. #read_dimmer -ch 0%       chX=Y->Z X=channel Y=state 0=Off 1=On Z=value
  if (str.startsWith("#read_dimmer"))
  {
    // if (str.length() == 18) {
    uint8_t channel = (str.substring(17)).toInt();
    if (channel != 1)
      channel = 0;
    Serial.printf("ch%d=%d->%d\n", channel, digitalRead(DIM_OFF[channel]), ledcRead(channel));
    return;
  }

  // 26. #on_dimmer -ch 0 -v 1%      v=0/1 off/on   ch0=0,1
  if (str.startsWith("#on_dimmer"))
  {
    uint8_t channel = (str.substring(15)).toInt();
    uint8_t onOff = (str.substring(20)).toInt();
    if (channel != 1)
      channel = 0;
    if (onOff != 1)
      onOff = 0;
    digitalWrite(DIM_OFF[channel], onOff);
    if (onOff == 1)
    {
      // Serial.printf("Create PWM channel %d on PIN=%d\n", channel, V_DIM[channel]);
      ledcSetup(channel, 5000, 8); // Канал 0, частота 5000 Гц, разрешение 8 бит (256 уровней)
      ledcAttachPin(V_DIM[channel], channel);
    }
    else
    {
      // Serial.printf("Clear PWM channel %d on PIN=%d\n",channel, V_DIM[channel]);
      ledcDetachPin(V_DIM[channel]);
    }
    Serial.printf("ch%d=%d\n", channel, digitalRead(DIM_OFF[channel]));
    return;
  }

  // 27. #set_dimmer -ch 0 -v 254%      v=0-255    ch=0,1
  if (str.startsWith("#set_dimmer"))
  {
    uint8_t channel = (str.substring(16)).toInt();
    if (channel != 1)
      channel = 0;
    uint8_t value = (str.substring(20, str.length())).toInt();
    ledcWrite(channel, value); // Устанавливаем на канал channel значение value
    Serial.printf("ch%d=%d\n", channel, value);
    return;
  }

  /*
  //  28. #test_button 10%         Pass if pressed / Fail if not pressed v= timeout for the button to be pressed in sec
  if (str.startsWith("#test_button")) {
    uint8_t timeOut = (str.substring(12, str.length())).toInt();
    if (testButton(timeOut)) {
      Serial.println("pressed");
    } else {
      Serial.println("not pressed");
    }
    return;
  }
  */

  //  28.1 #set_test_button 10%	10 = timeout for the button to be pressed in sec
  if (str.startsWith("#set_test_button"))
  {
    Serial.println("Ok");
    uint8_t timeOut = (str.substring(16, str.length())).toInt();
    resultTestButton = testButton(timeOut);
    return;
  }

  //  28.2 #get_test_button%
  if (str.startsWith("#get_test_button"))
  {
    if (resultTestButton)
    {
      Serial.println("pressed");
    }
    else
    {
      Serial.println("not pressed");
    }
    return;
  }

  // 29. #scan_i2c%
  if (str.startsWith("#scan_i2c"))
  {
    Serial.println(i2cScan());
    return;
  }

  // 30. #read_module_type%
  if (str.startsWith("#read_module_type"))
  {
    Serial.println(readModuleType());
    return;
  }

  // 31. #check_fs%
  if (str.startsWith("#check_fs"))
  {
    Serial.println(statusSPIFFS);
    return;
  }

  Serial.println("Incorrect command");
  Serial.println(str);
  Serial.println("Example:");
  Serial.println("#read_i2c -addr 112%");
  Serial.println("#read_mac_address%");
}

void setupDevicesForTest()
{
  Serial.println("-------setupDevicesForTest---------");

  pinMode(BUTTON_PIN, INPUT);

  if (moduleType == MODULE_TYPE_SENSOR || moduleType == MODULE_TYPE_HYDRO)
  {
    if (moduleType == MODULE_TYPE_SENSOR)
      Wire.begin(SDApin, SCLpin);

    for (int tst_i = 0; tst_i < 4; tst_i++)
    {
      pinMode(pin_SENSOR_PS[tst_i], OUTPUT);
      pinMode(pin_TST_SNS[tst_i], OUTPUT);
      digitalWrite(pin_TST_SNS[tst_i], HIGH); // Off reference voltage
    }
  }

  if (moduleSubType == MODULE_SUBTYPE_DIMMER)
  {
    for (int channel = 0; channel < 2; channel++)
    {
      pinMode(DIM_OFF[channel], OUTPUT);
      pinMode(V_DIM[channel], OUTPUT);
      digitalWrite(DIM_OFF[channel], OFF); // Off dimmer
    }
  }

  if (moduleType == MODULE_TYPE_DEVICE)
  {
    if (moduleSubType == MODULE_SUBTYPE_RELAY)
    {
      deviceCount = 4;

      for (byte i = 0; i < 4; i++)
      {
        pin[i] = pin_RELAY[i];
        LED_R[i] = LED_R_RELAY[i];
        LED_G[i] = LED_G_RELAY[i];
        pinMode(pin[i], OUTPUT);
        pinMode(LED_R[i], OUTPUT);
        pinMode(LED_G[i], OUTPUT);
      }
    }

    if (moduleSubType == MODULE_SUBTYPE_SOKET)
    {
      deviceCount = 2;

      for (byte i = 0; i < 2; i++)
      {
        pin[i] = pin_SKT[i];
        LED_R[i] = LED_R_SKT[i];
        LED_G[i] = LED_G_SKT[i];
        pinMode(pin[i], OUTPUT);
        pinMode(LED_R[i], OUTPUT);
        pinMode(LED_G[i], OUTPUT);
      }
    }
  }
}
