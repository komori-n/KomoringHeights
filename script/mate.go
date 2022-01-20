package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"sync"

	"github.com/schollz/progressbar"
)

type Options struct {
	HashSize   int
	DepthLimit int
	Process    int
}

func parseOptions() Options {
	hash_size := flag.Int("hash", 64, "the size of hash (MB)")
	depth_limit := flag.Int("mate-limit", 0, "the maximum mate length")
	num_process := flag.Int("process", 4, "the number of process")
	flag.Parse()

	return Options{HashSize: *hash_size, DepthLimit: *depth_limit, Process: *num_process}
}

type EngineProcess struct {
	cmd     *exec.Cmd
	stdin   io.WriteCloser
	stdout  io.ReadCloser
	scanner *bufio.Scanner
}

func newEngineProcess(command string) (*EngineProcess, error) {
	cmd := exec.Command(command)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	scanner := bufio.NewScanner(stdout)

	cmd.Start()
	return &EngineProcess{cmd, stdin, stdout, scanner}, nil
}

func (ep *EngineProcess) SetOption(op Options) {
	fmt.Fprintf(ep.stdin, "setoption name USI_Hash value %d\n", op.HashSize)
	fmt.Fprintf(ep.stdin, "setoption name DepthLimit value %d\n", op.DepthLimit)
	fmt.Fprintf(ep.stdin, "setoption name RootIsAndNodeIfChecked value false\n")
}

func (ep *EngineProcess) Ready() error {
	fmt.Fprintln(ep.stdin, "isready")

	for ep.scanner.Scan() {
		text := ep.scanner.Text()
		if strings.Contains(text, "readyok") {
			return nil
		}
	}

	err := ep.scanner.Err()
	if err != nil {
		return err
	}

	return fmt.Errorf("got no \"readyok\"")
}

func (ep *EngineProcess) GoMate(sfen string) (int, error) {
	fmt.Fprintf(ep.stdin, "sfen %s\n", sfen)
	fmt.Fprintln(ep.stdin, "go mate 10000")

	r := regexp.MustCompile(`.*num_searched=(\d+).*`)
	num_searched := 0
	for ep.scanner.Scan() {
		text := ep.scanner.Text()
		switch {
		case r.MatchString(text):
			num, err := strconv.Atoi(r.FindStringSubmatch(text)[1])
			if err == nil {
				num_searched = num
			}
		case strings.Contains(text, "nomate"):
			return num_searched, fmt.Errorf("got nomate")
		case strings.Contains(text, "checkmate "):
			if text == "checkmate " {
				return num_searched, fmt.Errorf("got checkout without mate moves")
			} else {
				return num_searched, nil
			}
		}
	}

	err := ep.scanner.Err()
	if err != nil {
		return num_searched, err
	}

	return num_searched, fmt.Errorf("got no \"readyok\"")
}

func solve(wg *sync.WaitGroup, bar *progressbar.ProgressBar, command string, op Options, sfen_input chan string) {
	defer wg.Done()

	process, err := newEngineProcess(command)
	if err != nil {
		fmt.Println("error:", err)
		os.Exit(2)
	}
	process.SetOption(op)
	err = process.Ready()
	if err != nil {
		fmt.Println("error:", err)
		os.Exit(2)
	}

	for sfen := range sfen_input {
		_, err := process.GoMate(sfen)
		if err != nil {
			fmt.Println("error:", err)
			fmt.Println("sfen", sfen)
		}
		bar.Add(1)
	}
}

func main() {
	op := parseOptions()

	if flag.NArg() == 0 {
		fmt.Println("error: solver command was not specified")
		flag.Usage()
		os.Exit(2)
	}

	bar := progressbar.Default(-1)

	command := flag.Arg(0)
	sfen_chan := make(chan string)
	var wg sync.WaitGroup
	for i := 0; i < op.Process; i++ {
		wg.Add(1)
		go solve(&wg, bar, command, op, sfen_chan)
	}

	sfen_scanner := bufio.NewScanner(os.Stdin)
	for sfen_scanner.Scan() {
		sfen := sfen_scanner.Text()
		sfen_chan <- sfen
	}
	close(sfen_chan)

	wg.Wait()
}
