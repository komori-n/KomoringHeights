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
)

type Options struct {
	HashSize   int
	DepthLimit int
}

func parseOptions() Options {
	hash_size := flag.Int("hash", 64, "the size of hash (MB)")
	depth_limit := flag.Int("mate-limit", 0, "the maximum mate length")
	flag.Parse()

	return Options{HashSize: *hash_size, DepthLimit: *depth_limit}
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
	fmt.Fprintln(ep.stdin, "go mate infinite")

	r := regexp.MustCompile(`.*num_searched=(\d+).*`)
	num_searched := 0
	for ep.scanner.Scan() {
		text := ep.scanner.Text()
		fmt.Println(text)
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

func main() {
	op := parseOptions()

	if flag.NArg() == 0 {
		fmt.Println("error: solver command was not specified")
		flag.Usage()
		os.Exit(2)
	}

	command := flag.Arg(0)
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

	num_searched := 0
	sfen_cnt := 1
	sfen_scanner := bufio.NewScanner(os.Stdin)
	for sfen_scanner.Scan() {
		sfen := sfen_scanner.Text()
		fmt.Printf("[%d] sfen %s\n", sfen_cnt, sfen)
		num, err := process.GoMate(sfen)
		fmt.Println("")

		if err != nil {
			fmt.Println("error:", err)
			break
		}
		num_searched += num

		sfen_cnt += 1
	}
	fmt.Println("num_searched:", num_searched)
}
