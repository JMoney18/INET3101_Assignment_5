from pathlib import Path
import socket
import time

HOST = "127.0.0.1"
PORT = 8080
TIMEOUT = 8


def request(payload):
    """Send raw bytes and collect the response until the server closes."""
    with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as connection:
        connection.settimeout(TIMEOUT)
        connection.sendall(payload)

        response = bytearray()

        while True:
            chunk = connection.recv(1024)

            if not chunk:
                break

            response.extend(chunk)

        return bytes(response)

def prepare_test_files():
    """Create the fixtures used by the tests. Run from the server's folder."""
    Path("example.txt").write_bytes(b"Hello from Assignment 5!\n")
    Path("oversized.txt").write_bytes(b"A" * 256)
    Path("boundary.txt").write_bytes(b"A" * 254 + b"\n")

    outside_file = Path("../assignment5_outside_test.txt")

    if not outside_file.exists():
        outside_file.write_bytes(b"Symlink test content\n")

    link = Path("link_test.txt")

    if not link.is_symlink() and not link.exists():
        link.symlink_to("../assignment5_outside_test.txt")
    elif not link.is_symlink():
        raise RuntimeError(
            "link_test.txt already exists and is not a symbolic link."
        )

    if Path("missing.txt").exists() or Path("missing.txt").is_symlink():
        raise RuntimeError(
            "missing.txt must not exist for the missing-file test."
        )

def main():
    prepare_test_files()
    
    expected = b"Hello from Assignment 5!\n"
    actual = request(b"example.txt\n")

    if actual != expected:
        raise AssertionError(
            f"Normal request failed: expected {expected!r}, got {actual!r}"
        )

    print("PASS: Normal file request")

    invalid_paths = [
        ("Relative path traversal", b"../example.txt\n"),
        ("Nested path traversal", b"../../etc/passwd\n"),
        ("Absolute path", b"/etc/passwd\n"),
    ]

    file_tests = [
        (
            "Missing file",
            b"missing.txt\n",
            b"ERROR: File Not Found.\n",
        ),
        (
            "Oversized file",
            b"oversized.txt\n",
            b"ERROR: File exceeds 255 bytes.\n",
        ),
        (
            "Exactly 255 bytes",
            b"boundary.txt\n",
            b"A" * 254 + b"\n",
        ),
    ]

    for name, payload, expected_response in file_tests:
        actual = request(payload)

        if actual != expected_response:
            raise AssertionError(
                f"{name}: expected {expected_response!r}, got {actual!r}"
            )

        actual = request(b"example.txt\n")

        if actual != expected:
            raise AssertionError(
                f"{name}: recovery request returned {actual!r}"
            )

        print(f"PASS: {name}; recovery succeeded")

    expected_error = b"ERROR: Invalid filename or path traversal.\n"

    for name, payload in invalid_paths:
        actual = request(payload)

        if actual != expected_error:
            raise AssertionError(
                f"{name}: expected {expected_error!r}, got {actual!r}"
            )

        # Confirm the server still serves valid requests after each attack.
        actual = request(b"example.txt\n")

        if actual != expected:
            raise AssertionError(
                f"{name}: recovery request returned {actual!r}"
            )

        print(f"PASS: {name} rejected; recovery succeeded")

    try:
        actual = request(b"example\x00.txt\n")
    except ConnectionResetError:
        actual = b""

    if actual != b"":
        raise AssertionError(
            f"Null-byte request returned unexpected data: {actual!r}"
        )

    actual = request(b"example.txt\n")

    if actual != expected:
        raise AssertionError(
            f"Null-byte recovery request returned {actual!r}"
        )

    print("PASS: Null-byte request rejected; recovery succeeded")

    try:
        actual = request(b"A" * 10000)
    except (ConnectionResetError, BrokenPipeError):
        actual = b""

    if actual != b"":
        raise AssertionError(
            f"Flood request returned unexpected data: {actual!r}"
        )

    actual = request(b"example.txt\n")

    if actual != expected:
        raise AssertionError(
            f"Flood recovery request returned {actual!r}"
        )

    print("PASS: 10,000-byte flood rejected; recovery succeeded")

    disconnect_tests = [
        ("Immediate disconnect", b""),
        ("Partial-request disconnect", b"exam"),
    ]

    for name, payload in disconnect_tests:
        with socket.create_connection(
            (HOST, PORT), timeout=TIMEOUT
        ) as connection:
            if payload:
                connection.sendall(payload)

        # Leaving the with block closes the connection.
        actual = request(b"example.txt\n")

        if actual != expected:
            raise AssertionError(
                f"{name}: recovery request returned {actual!r}"
            )

        print(f"PASS: {name}; recovery succeeded")

    with socket.create_connection(
        (HOST, PORT), timeout=TIMEOUT
    ) as connection:
        connection.settimeout(TIMEOUT)

        # Send nothing. The server should close after its inactivity timeout.
        actual = connection.recv(1024)

        if actual != b"":
            raise AssertionError(
                f"Idle connection returned unexpected data: {actual!r}"
            )

    actual = request(b"example.txt\n")

    if actual != expected:
        raise AssertionError(
            f"Timeout recovery request returned {actual!r}"
        )

    print("PASS: Idle client timed out; recovery succeeded")

    actual = request(b"link_test.txt\n")
    expected_error = b"ERROR: Could not open requested file.\n"

    if actual != expected_error:
        raise AssertionError(
            f"Symlink test: expected {expected_error!r}, got {actual!r}"
        )

    actual = request(b"example.txt\n")

    if actual != expected:
        raise AssertionError(
            f"Symlink recovery request returned {actual!r}"
        )

    print("PASS: Symbolic link rejected; recovery succeeded")

    with socket.create_connection(
        (HOST, PORT), timeout=TIMEOUT
    ) as connection:
        connection.settimeout(TIMEOUT)

        for piece in (b"exam", b"ple", b".txt", b"\n"):
            connection.sendall(piece)
            time.sleep(0.1)

        response = bytearray()

        while True:
            chunk = connection.recv(1024)

            if not chunk:
                break

            response.extend(chunk)

    if bytes(response) != expected:
        raise AssertionError(
            f"Fragmented request returned {bytes(response)!r}"
        )

    print("PASS: Filename sent in separate pieces")


if __name__ == "__main__":
    main()