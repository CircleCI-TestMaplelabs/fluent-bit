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
	"regexp"
	"strings"
)

var Log *log.Logger

const LOGPATH = "/var/log/sfagent/url-normalizer.log"

var port = flag.Int("port", 8587, "Socket server port")
var isStringWithDigits = regexp.MustCompile(`.*[0-9]+.*`).MatchString

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
	LaunchUrlNormalizer(ctx, *port)
}

// LaunchUrlNormalizer to launch the tcp socket server
func LaunchUrlNormalizer(ctx context.Context, port int) {
	Log.Printf("Launching URL Normalizer on port %d", port)
	socketServer(ctx, port)
}

func urlNormalizer(urlPath string) []byte {
	urlPath = strings.TrimPrefix(urlPath, "/")
	normalizedString := "/"
	for _, splitData := range strings.Split(urlPath, "/") {
		if isStringWithDigits(splitData) {
			normalizedString = normalizedString + "[norm]" + "/"
		} else {
			normalizedString = normalizedString + splitData + "/"
		}
	}
	result := fmt.Sprintf("normalized_url}%s",
		normalizedString,
	)
	return []byte(result)
}

func init() {
	flag.Parse()
}
