class SonyCameraWebserver < Formula
  desc "REST API server for Sony Camera Remote SDK"
  homepage "https://crsdk.app/"
  url "https://github.com/jordlee/alpha-sdk-api/releases/download/v2.1.0/CameraWebApp-macos.tar.gz"
  sha256 "72bff35ea89759992c04b30203545bdfb21ca52a010374a97f6ae076a5ce3463"
  version "2.1.0"
  license "MIT"

  depends_on :macos

  def install
    bin.install "CameraWebApp"
  end

  def caveats
    <<~EOS
      Sony Camera WebServer has been installed!

      To start the server:
        CameraWebApp

      The server will run on http://localhost:8080

      Client libraries and SDK docs:
        https://crsdk.app/sdk/overview

      Documentation: #{homepage}
    EOS
  end

  test do
    system "#{bin}/CameraWebApp", "--version"
  end
end
