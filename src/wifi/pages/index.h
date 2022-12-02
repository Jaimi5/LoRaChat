#pragma once

#include <Arduino.h>

//TODO: If you want to save some memory, you can remove the spaces and line breaks from the HTML code

const char indexHeaderHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <link rel='icon' href='data:,'>
    <style>
        html {
            font-family: Helvetica;
            display: inline-block;
            margin: 0px auto;
            text-align: center;
        }
        
        .button {
            background-color: #4CAF50;
            border: none;
            color: white;
            padding: 16px 40px;
            text-decoration: none;
            font-size: 30px;
            margin: 2px;
            cursor: pointer;
        }
        
        dl {
            max-width: fit-content;
            border: solid #333;
            border-width: 1px 1px 0px 1px;
        }
        
        dt {
            flex-basis: 20%;
            padding: 2px 4px;
            background: #333;
            color: #fff;
        }
        
        dd {
            flex-basis: 70%;
            flex-grow: 1;
            margin: 0;
            padding: 2px 4px;
            border-bottom: 1px solid #333;
        }

        .commands {
            text-align: -webkit-center;
        }
    </style>
</head>
<body>
)rawliteral";

const char indexBodyHTML[] PROGMEM = R"rawliteral(

    <h1>ESP32 Web Server</h1>
    <p>Blink Led </p>
    <p><a href='blinkLed'><button class='button'>Blink Led</button></a></p>

)rawliteral";

const char indexFooterHTML[] PROGMEM = R"rawliteral(
</body>
</html>
)rawliteral";