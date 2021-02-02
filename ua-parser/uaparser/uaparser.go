// Copyright (c) 2020 MapleLabs Inc*
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/ua-parser/uap-go/uaparser"
)

var (
	parser *uaparser.Parser
	Log    *log.Logger
)

const LOGPATH = "/var/log/sfagent/uaparser.log"

var port = flag.Int("port", 8586, "Socket server port")

func main() {
	ctx := context.Background()
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	file, err := os.OpenFile(LOGPATH, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		log.Fatalf("error opening file: %v", err)
	}
	defer file.Close()

	log.SetOutput(file)
	Log = log.New(file, "", log.LstdFlags|log.Lshortfile)
	LaunchUAParser(ctx, *port)
}

// LaunchUAParser to launch the tcp socket server
func LaunchUAParser(ctx context.Context, port int) {
	Log.Printf("Launching UA Parser on port %d", port)
	var err error
	parser, err = uaparser.NewFromBytes([]byte(regexes))
	if err != nil {
		log.Fatalf("Error in UAParser server:%v", err)
	}
	socketServer(ctx, port)
}

func agentParsing(uagent string) []byte {
	var result, browser, os string
	browserVersion := "Unknown"
	osVersion := "Unknown"
	client := parser.Parse(uagent)
	if client.UserAgent.Major != "" {
		browserVersion = client.UserAgent.Major
		if client.UserAgent.Minor != "" {
			browserVersion = browserVersion + "." + client.UserAgent.Minor
			if client.UserAgent.Patch != "" {
				browserVersion = browserVersion + "." + client.UserAgent.Patch
			}
		}
	}
	if browserVersion != "Unknown" {
		browser = client.UserAgent.Family + " " + browserVersion
	} else {
		browser = client.UserAgent.Family
	}
	if client.Os.Major != "" {
		osVersion = client.Os.Major
		if client.Os.Minor != "" {
			osVersion = osVersion + "." + client.Os.Minor
			if client.Os.Patch != "" {
				osVersion = osVersion + "." + client.Os.Patch
				if client.Os.PatchMinor != "" {
					osVersion = osVersion + "." + client.Os.PatchMinor
				}
			}
		}
	}
	if osVersion != "Unknown" {
		os = client.Os.Family + " " + osVersion
	} else {
		os = client.Os.Family
	}
	var brand, model string
	if client.Device.Brand != "" {
		brand = client.Device.Brand
	} else {
		brand = "Other"
	}
	if client.Device.Model != "" {
		model = client.Device.Model
	} else {
		model = "Other"
	}
	result = fmt.Sprintf("browser_name}%s}browser_version}%s}browser}%s}OS_name}%s}OS_version}%s}OS}%s}device}%s}device_brand}%s}device_model}%s",
		client.UserAgent.Family,
		browserVersion,
		browser,
		client.Os.Family,
		osVersion,
		os,
		client.Device.Family,
		brand,
		model,
	)
	return []byte(result)
}

func init() {
	flag.Parse()
}
