// Copyright (c) 2020 MapleLabs Inc*
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

package main

import (
	"bufio"
	"context"
	"net"
	"os"
	"strconv"
	"strings"
)

const (
	connHost = "localhost"
	connType = "tcp"
)

func socketServer(ctx context.Context, port int) {
	connChan := make(chan net.Conn)
	Log.Printf("[URL Normalizer] Starting %s server on %s:%s", connType, connHost, strconv.Itoa(port))
	l, err := net.Listen(connType, connHost+":"+strconv.Itoa(port))
	if err != nil {
		Log.Printf("[URL Normalizer] Error listening:%v", err.Error())
		os.Stderr.WriteString(err.Error())
		os.Exit(1)
	}
	defer l.Close()
	for {
		go receiveConn(ctx, l, connChan) // One extra goroutine always running to keep listening for the new connections.
		select {
		case <-ctx.Done():
			Log.Println("stopped url_normalizer socket server")
			return
		case c := <-connChan:
			go func() {
				handleConnection(ctx, c)
				defer c.Close()
			}()
		}

	}
}

func receiveConn(ctx context.Context, listener net.Listener, connChan chan net.Conn) {
	// Blocking statement. waits for a new connection.
	c, err := listener.Accept()
	if err != nil {
		select {
		case <-ctx.Done():
			return
		default:
			Log.Println("[URL Normalizer] Error connecting:", err.Error())
			return
		}
	}
	Log.Printf("[URL Normalizer] Client %s connected", c.RemoteAddr().String())
	connChan <- c
}

func handleConnection(ctx context.Context, conn net.Conn) {
	closeChan := make(chan bool)
	urlPathChan := make(chan []byte, 3)

	// defer conn.Close()
	for {
		go receiveAgent(conn, urlPathChan, closeChan)
		select {
		case <-ctx.Done():
			return
		case <-closeChan:
			Log.Printf("[URL Normalizer] Connection closed by client: %s", conn.RemoteAddr().String())
			return
		case urlPath := <-urlPathChan:
			if _, err := conn.Write(urlNormalizer(strings.TrimSuffix(string(urlPath), "\n"))); err != nil {
				Log.Printf("[URL Normalizer] Error while Write to connection : %+v", err)
			}
		}
	}
}

func receiveAgent(conn net.Conn, urlPathChan chan []byte, closeChan chan bool) {
	buffer, err := bufio.NewReader(conn).ReadBytes('\n')
	if err != nil {
		Log.Printf("[URL Normalizer] Client %s exit (%s)", conn.RemoteAddr().String(), err.Error())
		closeChan <- true
		return
	}
	urlPathChan <- buffer
}
