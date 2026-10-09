import http.client
import os
import sys
import threading
import urllib.error
import urllib.request

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import fw_server as fs  # noqa: E402

IMAGE = bytes(range(256)) * 40


@pytest.fixture
def release(tmp_path):
    (tmp_path / "firmware.bin").write_bytes(IMAGE)
    (tmp_path / "manifest.bin").write_bytes(b"M" * 192)
    return tmp_path


def serve(directory, faults):
    server = fs.make_server(directory, 0, faults, bind="127.0.0.1")
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def get(url):
    with urllib.request.urlopen(url, timeout=5) as response:
        return response.status, response.headers, response.read()


def test_body_without_faults_is_the_file():
    assert fs.body_for("firmware.bin", IMAGE, fs.Faults(), 1) == (IMAGE, len(IMAGE))


def test_flip_changes_one_byte_of_the_image_only():
    body, length = fs.body_for("firmware.bin", IMAGE, fs.Faults(flip_byte=100), 1)
    assert length == len(IMAGE)
    assert [i for i in range(len(IMAGE)) if body[i] != IMAGE[i]] == [100]
    assert fs.body_for("manifest.bin", IMAGE, fs.Faults(flip_byte=100), 1)[0] == IMAGE


def test_cut_announces_the_full_length_and_sends_part():
    body, length = fs.body_for("firmware.bin", IMAGE, fs.Faults(cut_at=0.5), 1)
    assert length == len(IMAGE) and body == IMAGE[:len(IMAGE) // 2]


def test_cut_only_hits_the_first_requests():
    faults = fs.Faults(cut_at=0.5, cut_times=2)
    assert len(fs.body_for("firmware.bin", IMAGE, faults, 2)[0]) == len(IMAGE) // 2
    assert fs.body_for("firmware.bin", IMAGE, faults, 3)[0] == IMAGE


def test_serves_the_release_files(release):
    server, base = serve(release, fs.Faults())
    try:
        status, headers, body = get(base + "firmware.bin")
        assert status == 200 and body == IMAGE
        assert headers["Content-Length"] == str(len(IMAGE))
        assert get(base + "manifest.bin")[2] == b"M" * 192
    finally:
        server.shutdown()


def test_cut_download_ends_early(release):
    server, base = serve(release, fs.Faults(cut_at=0.5, cut_times=1))
    try:
        with pytest.raises(http.client.IncompleteRead):
            get(base + "firmware.bin")
        assert get(base + "firmware.bin")[2] == IMAGE
    finally:
        server.shutdown()


@pytest.mark.parametrize("path", ["missing.bin", "../firmware.bin", "sub/firmware.bin"])
def test_unknown_paths_are_404(release, path):
    server, base = serve(release, fs.Faults())
    try:
        with pytest.raises(urllib.error.HTTPError) as error:
            get(base + path)
        assert error.value.code == 404
    finally:
        server.shutdown()


def test_missing_image_fault_is_404(release):
    server, base = serve(release, fs.Faults(missing_image=True))
    try:
        with pytest.raises(urllib.error.HTTPError) as error:
            get(base + "firmware.bin")
        assert error.value.code == 404
        assert get(base + "manifest.bin")[0] == 200
    finally:
        server.shutdown()
