/*
 * SPDX-FileCopyrightText: 2026-2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"
)

// lotus-engine-helper replaces the cgo c-archive that lotus.so used to link:
// a fully static pure-Go binary (CGO_ENABLED=0) that runs on any libc, musl
// included. The name follows the Unix helper-process convention: spawned by
// the addon, serving exactly one client, dying with it.
//
// The addon spawns us with stdout wired to a pipe; we print the name of a
// freshly bound abstract unix socket and serve that one connection to EOF.
func main() {
	signal.Ignore(syscall.SIGPIPE)

	name := fmt.Sprintf("lotus-engine-%d-%d", os.Getpid(), time.Now().UnixNano())
	listener, err := net.Listen("unix", "@"+name)
	if err != nil {
		fmt.Fprintf(os.Stderr, "lotus-engine-helper: cannot listen on abstract socket: %v\n", err)
		os.Exit(1)
	}

	// Announce the socket name to the parent (stdout is its pipe).
	fmt.Println(name)

	conn, err := listener.Accept()
	listener.Close()
	if err != nil {
		fmt.Fprintf(os.Stderr, "lotus-engine-helper: accept: %v\n", err)
		os.Exit(1)
	}
	serve(conn.(*net.UnixConn))
}

// request is one decoded client message plus any descriptors received with it.
type request struct {
	op      byte
	handle  uint64
	payload []byte
	fds     []int
}

// response is what dispatch produced for a request.
type response struct {
	status  byte
	payload []byte
}

// serve runs the connection until EOF or error, then returns so main can exit.
func serve(conn *net.UnixConn) {
	defer conn.Close()

	if !handshake(conn) {
		return
	}
	for {
		req, err := readRequest(conn)
		if err != nil {
			return
		}
		resp := dispatch(req)
		if err := writeMessage(conn, resp.status, resp.payload); err != nil {
			return
		}
	}
}

// handshake validates the protocol version, closing on mismatch so the client
// respawns a matching helper.
func handshake(conn *net.UnixConn) bool {
	op, _, payload, _, err := readMessage(conn)
	if err != nil || op != opHello || len(payload) < 4 {
		return false
	}
	if binary.LittleEndian.Uint32(payload) != protocolVersion {
		writeMessage(conn, statusError, nil)
		return false
	}
	return writeMessage(conn, statusOk, protocolVersionBytes()) == nil
}

func protocolVersionBytes() []byte {
	var b [4]byte
	binary.LittleEndian.PutUint32(b[:], protocolVersion)
	return b[:]
}

// readMessage reads one framed message: op, handle, payload and any SCM_RIGHTS
// descriptors (fds travel with the first bytes, hence ReadMsgUnix).
func readMessage(conn *net.UnixConn) (byte, uint64, []byte, []int, error) {
	var hdr [13]byte
	oob := make([]byte, 64) // room for one SCM_RIGHTS cmsg with a few fds
	n, oobn, _, _, err := conn.ReadMsgUnix(hdr[:], oob)
	if err != nil {
		return 0, 0, nil, nil, err
	}
	if n < len(hdr) {
		return 0, 0, nil, nil, io.ErrUnexpectedEOF
	}
	var fds []int
	if oobn > 0 {
		fds, err = parseFds(oob[:oobn])
		if err != nil {
			return 0, 0, nil, nil, err
		}
	}
	total := int(binary.LittleEndian.Uint32(hdr[0:4]))
	if total < len(hdr) || total > maxMessageSize {
		return 0, 0, nil, nil, errors.New("lotus-engine-helper: bad message length")
	}
	payload := make([]byte, total-len(hdr))
	if _, err := io.ReadFull(conn, payload); err != nil {
		return 0, 0, nil, nil, err
	}
	return hdr[4], binary.LittleEndian.Uint64(hdr[5:13]), payload, fds, nil
}

// parseFds extracts descriptors from SCM_RIGHTS control messages.
func parseFds(oob []byte) ([]int, error) {
	var fds []int
	controls, err := syscall.ParseSocketControlMessage(oob)
	if err != nil {
		return nil, err
	}
	for _, c := range controls {
		if c.Header.Level != syscall.SOL_SOCKET || c.Header.Type != syscall.SCM_RIGHTS {
			continue
		}
		data := c.Data
		for len(data) >= 4 {
			fds = append(fds, int(binary.LittleEndian.Uint32(data[:4])))
			data = data[4:]
		}
	}
	return fds, nil
}

// readRequest is readMessage with the request type spelled out.
func readRequest(conn *net.UnixConn) (request, error) {
	op, handle, payload, fds, err := readMessage(conn)
	if err != nil {
		return request{}, err
	}
	return request{op: op, handle: handle, payload: payload, fds: fds}, nil
}

// writeMessage frames and sends status+payload.
func writeMessage(conn *net.UnixConn, status byte, payload []byte) error {
	buf := make([]byte, 5+len(payload))
	binary.LittleEndian.PutUint32(buf[0:4], uint32(len(buf)))
	buf[4] = status
	copy(buf[5:], payload)
	_, err := conn.Write(buf)
	return err
}

// closeUnusedFds closes received-but-unconsumed descriptors so no fd leaks.
func closeUnusedFds(fds []int) {
	for _, fd := range fds {
		syscall.Close(fd)
	}
}
