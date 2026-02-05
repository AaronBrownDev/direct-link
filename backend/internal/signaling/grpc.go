package signaling

import (
	"encoding/json"
	"io"

	pb "github.com/AaronBrownDev/direct-link/gen/proto/signaling"
	"github.com/pion/ion-sfu/pkg/sfu"
	"github.com/pion/webrtc/v3"
)

// Signal handles bidirectional streaming for WebRTC signaling
func (s *Server) Signal(stream pb.SignalingService_SignalServer) error {

	// Peer represents one client's WebRTC connection
	peer := sfu.NewPeer(s.sfu)
	defer peer.Close()

	s.logger.Info("new peer connected")

	// Set up callbacks (SFU -> Client)

	// Called when ion-sfu finds a new ICE candidate.
	peer.OnIceCandidate = func(candidate *webrtc.ICECandidateInit, target int) {

		// Convert candidate to JSON since gRPC expects string type
		candidateJSON, err := json.Marshal(candidate)
		if err != nil {
			s.logger.Error("failed to marshal ice candidate", "error", err)
			return
		}

		// Send to client via gRPC stream
		err = stream.Send(&pb.SignalReply{
			Payload: &pb.SignalReply_Trickle{
				Trickle: &pb.Trickle{
					Target:    pb.Trickle_Target(target), // Publisher or Subscriber
					Candidate: string(candidateJSON),
				},
			},
		})
		if err != nil {
			s.logger.Error("failed to send trickle", "error", err)
		}

	}

	// Called when ion-sfu needs to renegotiate the connection.
	peer.OnOffer = func(offer *webrtc.SessionDescription) {
		err := stream.Send(&pb.SignalReply{
			Payload: &pb.SignalReply_Description{
				Description: &pb.SessionDescription{
					Type: offer.Type.String(), // "offer"
					Sdp:  []byte(offer.SDP),
				},
			},
		})
		if err != nil {
			s.logger.Error("failed to send offer", "offer", err)
		}
	}

	// Message loop (Client -> SFU direction)

	for {
		// Blocks and waits until next message is received
		req, err := stream.Recv()
		if err == io.EOF {
			s.logger.Info("peer disconnected")
			return nil
		}
		if err != nil {
			s.logger.Error("stream error", "error", err)
			return err
		}

		switch payload := req.Payload.(type) {

		// JOIN: Client wants to join a session/room
		case *pb.SignalRequest_Join:
			s.logger.Info("peer joining session",
				"session_id", payload.Join.SessionId,
				"user_id", payload.Join.UserId)

			// Joins session / registers peer
			err := peer.Join(payload.Join.SessionId, payload.Join.UserId)
			if err != nil {
				s.logger.Error("failed to join session", "error", err)
				// TODO: send error reply to client
				continue
			}

			// Parse SDP offer from client.
			// SDP describes their WebRTC capabilities
			offer := webrtc.SessionDescription{
				Type: webrtc.SDPTypeOffer,
				SDP:  string(payload.Join.Offer),
			}

			// Generates SDP answer
			answer, err := peer.Answer(offer)
			if err != nil {
				s.logger.Error("failed to create answer", "error", err)
				// TODO: send error reply to client
				continue
			}

			// Sends answer to client
			err = stream.Send(&pb.SignalReply{
				Payload: &pb.SignalReply_Join{
					Join: &pb.JoinReply{
						Answer: []byte(answer.SDP),
					},
				},
			})
			if err != nil {
				s.logger.Error("failed to send join reply", "error", err)
				return err
			}

			s.logger.Info("peer joined session", "session_id", payload.Join.SessionId)

		// TRICKLE: Client is sending an ICE candidate
		case *pb.SignalRequest_Trickle:

			var candidate webrtc.ICECandidateInit
			err := json.Unmarshal([]byte(payload.Trickle.Candidate), &candidate)
			if err != nil {
				s.logger.Error("failed to parse ICE candidate", "error", err)
				continue
			}

			target := int(payload.Trickle.Target) // Publisher or subscriber
			err = peer.Trickle(candidate, target)
			if err != nil {
				s.logger.Error("failed to trickle", "error", err)
			}

		// DESCRIPTION: Renegotiation (SDP offer or answer)
		case *pb.SignalRequest_Description:

			sdpType := payload.Description.Type // "offer" or "answer"
			sdp := string(payload.Description.Sdp)

			if sdpType == "answer" {
				// Client answered offer
				answer := webrtc.SessionDescription{
					Type: webrtc.SDPTypeAnswer,
					SDP:  sdp,
				}

				err := peer.SetRemoteDescription(answer)
				if err != nil {
					s.logger.Error("failed to set remote description", "error", err)
				}
			} else if sdpType == "offer" {
				// Client sent new offer
				offer := webrtc.SessionDescription{
					Type: webrtc.SDPTypeOffer,
					SDP:  sdp,
				}
				answer, err := peer.Answer(offer)
				if err != nil {
					s.logger.Error("failed to answer offer", "error", err)
					continue
				}

				err = stream.Send(&pb.SignalReply{
					Payload: &pb.SignalReply_Description{
						Description: &pb.SessionDescription{
							Type: answer.Type.String(),
							Sdp:  []byte(answer.SDP),
						},
					},
				})
				if err != nil {
					s.logger.Error("failed to send answer", "error", err)
				}
			}

		default:
			s.logger.Warn("unknown message type")
		}

	}
}
