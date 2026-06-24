package main

import (
	"bufio"
	"embed"
	"encoding/json"
	"fmt"
	"io/fs"
	"net"
	"net/http"
	"os"
	"sync"
	"time"
)

//go:embed static
var staticFiles embed.FS

// ბოლო სენსორის მონაცემები
var (
	latestData  SensorData
	lastUpdated time.Time
	mu          sync.RWMutex
)

type SensorData struct {
	Name        string  `json:"name"`
	Temperature float64 `json:"temperature"`
	Humidity    float64 `json:"humidity"`
	DistanceMM  uint16  `json:"distance_mm"`
	Occupied    bool    `json:"occupied"`
	Status      bool    `json:"status"`
}

type APIResponse struct {
	SensorData
	LastUpdated string `json:"last_updated"`
}

func main() {
	// HTTP სერვერი
	go startHTTP()

	// TCP სერვერი ESP32-სთვის
	ln, err := net.Listen("tcp", ":5678")
	if err != nil {
		fmt.Println("Error starting TCP server:", err)
		os.Exit(1)
	}
	defer ln.Close()

	fmt.Println("TCP server listening on :5678")

	for {
		conn, err := ln.Accept()
		if err != nil {
			fmt.Println("Error accepting connection:", err)
			continue
		}
		go handleConnection(conn)
	}
}

func startHTTP() {
	// embed-ი "static" დირექტორიას შეიცავs; fs.Sub-ით root-ს ვამისამართებთ
	// უშუალოდ static/-ზე, რომ "/" პირდაპირ index.html-ს აწვდიდეს.
	staticRoot, err := fs.Sub(staticFiles, "static")
	if err != nil {
		fmt.Println("Error setting up static FS:", err)
		os.Exit(1)
	}

	http.Handle("/", http.FileServer(http.FS(staticRoot)))
	http.HandleFunc("/api/sensor", handleSensorAPI)

	fmt.Println("HTTP server listening on :8080")
	if err := http.ListenAndServe(":8080", nil); err != nil {
		fmt.Println("HTTP server error:", err)
	}
}

func handleSensorAPI(w http.ResponseWriter, r *http.Request) {
	mu.RLock()
	resp := APIResponse{
		SensorData:  latestData,
		LastUpdated: lastUpdated.Format("2006-01-02 15:04:05"),
	}
	mu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func handleConnection(conn net.Conn) {
	defer conn.Close()
	fmt.Printf("Client connected: %s\n", conn.RemoteAddr().String())

	for {
		message, err := receiveData(conn)
		if err != nil {
			fmt.Printf("Client disconnected or error: %v\n", err)
			break
		}

		var data SensorData
		if err := json.Unmarshal([]byte(message), &data); err != nil {
			fmt.Printf("[%s] JSON parse error: %v (raw: %s)\n", conn.RemoteAddr(), err, message)
			sendData(conn, `{"error":"invalid json"}`)
			continue
		}

		// შენახვა HTTP API-სთვის
		mu.Lock()
		latestData = data
		lastUpdated = time.Now()
		mu.Unlock()

		fmt.Printf("[%s] %s — ტემპერატურა: %.1f°C, ტენიანობა: %.1f%%, მანძილი: %d mm, ძაღლი: %v, სტატუსი: %v\n",
			conn.RemoteAddr(), data.Name, data.Temperature, data.Humidity, data.DistanceMM, data.Occupied, data.Status)

		response := fmt.Sprintf(`{"ok":true,"received_temp":%.1f,"received_hum":%.1f,"received_dist":%d,"occupied":%v}`,
			data.Temperature, data.Humidity, data.DistanceMM, data.Occupied)
		err = sendData(conn, response)
		if err != nil {
			fmt.Println("Error sending data:", err)
			break
		}
	}
}

func receiveData(conn net.Conn) (string, error) {
	reader := bufio.NewReader(conn)
	message, err := reader.ReadString('\n')
	if err != nil {
		return "", err
	}
	return message[:len(message)-1], nil
}

func sendData(conn net.Conn, message string) error {
	writer := bufio.NewWriter(conn)
	_, err := writer.WriteString(message + "\n")
	if err != nil {
		return err
	}
	return writer.Flush()
}
