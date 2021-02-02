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
)

const (
	connHost = "localhost"
	connType = "tcp"
)

func socketServer(ctx context.Context, port int) {
	connChan := make(chan net.Conn)
	Log.Printf("[UA Parser] Starting %s server on %s:%s", connType, connHost, strconv.Itoa(port))
	l, err := net.Listen(connType, connHost+":"+strconv.Itoa(port))
	if err != nil {
		Log.Printf("[UA Parser] Error listening:%v", err.Error())
		os.Stderr.WriteString(err.Error())
		os.Exit(1)
		return
	}
	defer l.Close()
	for {
		go receiveConn(ctx, l, connChan)
		select {
		case <-ctx.Done():
			Log.Println("stopped uaparser socket server")
			return
		case c := <-connChan:
			go handleConnection(ctx, c)
		}
	}
}

func receiveConn(ctx context.Context, listener net.Listener, connChan chan net.Conn) {
	c, err := listener.Accept()
	if err != nil {
		select {
		case <-ctx.Done():
			return
		default:
			Log.Println("[UA Parser] Error connecting:", err.Error())
			return
		}
	}
	Log.Printf("[UA Parser] Client %s connected", c.RemoteAddr().String())
	connChan <- c
}

func handleConnection(ctx context.Context, conn net.Conn) {
	closeChan := make(chan bool)
	agentChan := make(chan []byte, 3)

	defer conn.Close()
	for {
		go receiveAgent(conn, agentChan, closeChan)
		select {
		case <-ctx.Done():
			return
		case <-closeChan:
			Log.Printf("[UA Parser] Connection closed by client: %s", conn.RemoteAddr().String())
			return
		case agent := <-agentChan:
			if _, err := conn.Write(agentParsing(string(agent))); err != nil {
				Log.Printf("[UA Parser] Error while Write to connection : %+v", err)
			}
		}
	}
}

func receiveAgent(conn net.Conn, agentChan chan []byte, closeChan chan bool) {
	buffer, err := bufio.NewReader(conn).ReadBytes('\n')
	if err != nil {
		Log.Printf("[UA Parser] Client %s exit (%s)", conn.RemoteAddr().String(), err.Error())
		closeChan <- true
		return
	}
	agentChan <- buffer
}

