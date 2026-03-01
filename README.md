**Architecting a Low-Latency Multichannel Speech-to-Speech Video Calling Application**

NOTE: This document is in its early stages and hence is subject to changes as per the collective decision made by the contributors and the maintainers. Some open challenges are also there for which the community can send in their recommendations. 

The conceptualization and execution of a real-time, cross-lingual video calling platform represent one of the most demanding challenges in modern distributed systems engineering. Achieving fluid conversational dynamics requires the entire system to adhere to a strict Total Turn-Around Time (T-TAT) of under 500 milliseconds.1 Exceeding this threshold introduces perceptible lag that disrupts natural human interaction, rendering the speech-to-speech translation ineffective.1 The architectural blueprint proposed herein details a highly optimized, asynchronous topology designed to meet these exacting latency constraints.

The infrastructure deliberately bifurcates the media streams. Video is routed out-of-band directly between clients via WebRTC over STUN/TURN servers, avoiding the heavy computational overhead of the backend machine learning infrastructure.4 Conversely, the audio stream traverses a complex, low-latency pipeline. Raw audio is ingested at the edge via a Quarkus-powered WebSocket gateway, transported through the internal cluster using Aeron inter-process communication (IPC), and processed by the NVIDIA Triton Inference Server.6 Within Triton, the data is operated on by a suite of decoupled models to transcribe, translate, and synthesize the speech before it is routed back through Aeron and Quarkus to the receiving client.9

This report provides an exhaustive analysis of this architecture, evaluating the technical feasibility of integrating these disparate components, proposing engineering solutions for complex data routing, and culminating in a comprehensive, multi-phased project plan for deployment.

**Edge Ingestion and Egress: Quarkus WebSocket Optimization**

The ingress layer of the application acts as the critical boundary between the variable conditions of the public internet and the deterministic performance of the internal inference cluster. Traditional HTTP/REST paradigms are fundamentally unsuited for streaming real-time audio. The request-response model requires a new TCP connection and HTTP header parsing for every discrete data payload.2 For an application processing 10 to 20-millisecond audio frames, the 500 to 800 bytes of HTTP header overhead per request would exceed the size of the actual audio payload, resulting in massive bandwidth waste and unacceptable latency.2

To circumvent the limitations of traditional REST APIs, the architecture relies on the WebSocket protocol, documented in RFC6455.6 WebSockets establish a standardized method for creating a persistent, bidirectional communication channel between the client and the server over a single TCP connection.6 Once the initial HTTP handshake undergoes the 'Upgrade' transition to the WebSocket protocol, the TCP socket remains open, allowing the client and server to exchange messages continually with minimal framing overhead.6 The overhead is effectively amortized across the entire duration of the call, enabling the rapid transmission of audio chunks necessary for continuous inference.2

**Reactive Architecture and Binary Streaming**

The edge application layer is constructed using Quarkus, a Kubernetes-native Java stack tailored for low memory utilization and rapid startup times.11 Quarkus is built upon a highly optimized reactive core powered by Netty and Eclipse Vert.x.11 Rather than utilizing a traditional thread-per-request model, which can quickly lead to thread pool exhaustion and context-switching overhead under heavy concurrent load, the Quarkus reactive engine employs a small number of non-blocking event loops.11

To maximize throughput, the client must stream the audio data as raw binary frames (such as PCM or Opus encoded packets).14 Developers frequently make the error of Base64-encoding binary data to transmit it over text-based WebSocket channels. This approach inflates the payload size by approximately 33% and incurs significant CPU penalties on both the client and server for encoding and decoding the strings.14 By leveraging the quarkus-websockets-next extension, the application can declaratively define endpoints that natively accept and route byte arrays.15

Memory management is a paramount concern at this layer. Naive implementations of WebSocket servers in Java can lead to excessive heap memory allocation or high consumption of the Direct Byte Buffer pool, potentially triggering garbage collection pauses that disrupt the sub-millisecond latency requirements.16 To mitigate this, the Quarkus implementation must utilize non-blocking Uni\<Void\> return types for message handlers to ensure the event loop is never blocked.17 Furthermore, the application should implement custom object pooling for the byte buffers received from the WebSocket, immediately wrapping them into the structures required by the internal transport layer without intermediate array copying or instantiation.16

Connection resilience is maintained by configuring the quarkus.websockets-next.server.auto-ping-interval property, which allows the server to automatically transmit ping messages to connected clients.19 This ensures that "zombie" connections resulting from dropped mobile network signals are rapidly identified and pruned, freeing resources for active participants without relying on excessive application-level polling.19

**High-Performance Internal Transport: The Aeron Backbone**

Once the binary audio chunks traverse the Quarkus edge, they must be routed to the deep learning inference engine. The architectural mandate suggests using a "Kafka analogy" where data is pushed to separate topics for processing and retrieval. While the conceptual flow of pub/sub messaging is accurate, implementing Apache Kafka would introduce catastrophic latency into a real-time conversational pipeline.13 Kafka is designed for durability and scalability, persisting messages to distributed logs on disk.13 This disk-I/O overhead, even when heavily optimized, precludes the microsecond-level predictability required for feeding a streaming speech-to-speech model.

Instead, the architecture employs Aeron, a high-performance, open-source OSI Layer 4 messaging transport designed explicitly for ultra-low latency and high throughput.20 Aeron operates on a peer-to-peer paradigm, eliminating the need for centralized brokers.13 It delivers predictable latency, often under five microseconds, and supports throughputs exceeding tens of millions of messages per second.20

**Translating the Kafka Analogy to Aeron Topologies**

To implement the requested architecture using Aeron, the Kafka terminology of "topics" and "partitions" must be mapped to Aeron's structural concepts: Channels, Stream IDs, and Session IDs.13

|                                |                                   |                                       |
| :----------------------------: | :-------------------------------: | :-----------------------------------: |
| **Messaging Concept**      | **Apache Kafka Architecture** | **Aeron Architecture Equivalent** |
| **Routing Destination**    | Topic                             | Channel URI + Stream ID               |
| **Data Ingress**           | Producer                          | Publication                           |
| **Data Egress**            | Consumer                          | Subscription / Image                  |
| **Concurrency / Ordering** | Partition                         | Session ID                            |
| **Underlying Mechanism**   | Disk-backed Distributed Log       | Memory-Mapped Ring Buffers            |

Aeron utilizes unidirectional connections governed by a Media Driver, which handles all media-specific logic (such as UDP socket management or IPC shared memory operations) asynchronously, isolating the application threads from the network stack.21

The **Channel** is a URI that defines the transport medium.13 For microservices co-located on the same physical hardware or virtual machine, the aeron:ipc channel is utilized.13 This leverages Unix shared memory (/dev/shm) to create memory-mapped files.24 The Quarkus publisher and the inference subscriber read and write directly to these memory segments without any kernel intervention or network routing, yielding the absolute lowest possible latency.24 If the Quarkus API gateways and the Triton GPU servers are distributed across a network, the aeron:udp?endpoint=\<ip\>:\<port\> channel is configured to provide reliable, order-guaranteed delivery over standard UDP.13

The **Stream ID** is an application-defined integer used to multiplex different logical streams of messages over the same Channel.13 In the context of the S2S architecture, the system will define specific Stream IDs for routing. For instance, Stream ID 100 could represent inbound raw audio awaiting transcription, while Stream ID 200 represents the outbound synthesized translated audio.13

**Implementing Request IDs and Session Tracking**

The prompt explicitly requires the byte stream to be sent to Aeron with a "Request ID" to ensure that the translated audio can be patched back to the correct WebSocket connection. Aeron inherently tracks connections using **Session IDs**. When a new Publication is created by the Quarkus application, the Aeron Media Driver generates a randomized, unique Session ID for that specific publisher.7 A single Subscription listening on a specific Channel and Stream ID will aggregate data from multiple Session IDs into distinct log buffers known as Images.7 Aeron guarantees strict message ordering within a specific Session ID, but not across multiple Session IDs.7

While the Session ID identifies the source publication, a custom application-level Request ID (or Correlation ID) is required to track the conversational state across the asynchronous inference pipeline.26 Because Aeron treats the message body as opaque binary data, the Quarkus application must construct a custom binary header.18

Before calling the offer() method on the Aeron Publication, Quarkus utilizes an Agrona UnsafeBuffer to encode a fixed-layout message.18 This buffer will contain the globally unique Request ID (identifying the specific user or call session), sequence numbers to track chunk order, metadata dictating the source and target languages, and finally, the raw audio byte array.20 Using Simple Binary Encoding (SBE) for this structure minimizes CPU parsing cycles compared to JSON serialization.20

When the processed audio is eventually returned from the AI pipeline on the outbound Aeron Stream ID, the Quarkus Subscription polls the Image using a FragmentHandler.18 The handler decodes the SBE header, extracts the Request ID, looks up the corresponding active WebSocket session in a concurrent hash map, and dispatches the binary audio payload back to the originating client.7

**NVIDIA Triton Inference Server Orchestration**

The central query regarding the architecture is whether it is technically possible to feed chunked data directly from Aeron into a Triton Inference Server and push the processed translations back to Aeron.

**Feasibility and the Integration Bridge**

Triton Inference Server is an open-source inference serving software that optimizes the execution of AI models from multiple frameworks (TensorRT, PyTorch, ONNX) on GPU hardware.8 Out of the box, Triton exposes HTTP/REST and gRPC endpoints based on the KServe protocol.30 It does not possess native plugins or listeners for Aeron IPC or UDP channels.29 Therefore, directly connecting Aeron to Triton without an intermediary adaptation layer is impossible.

  

**For this we will have to create an  In-Process C-API Bridge**

**The In-Process C-API Bridge:** The architecturally sound solution is to develop a lightweight, dedicated proxy microservice—the Aeron-to-Triton Bridge. This application is written in C++ or Java and runs within the same container or bare-metal environment as the Triton Inference Server.32

This Bridge application acts as the Aeron Subscriber. It rapidly polls the Aeron memory-mapped files using Subscription.poll(fragmentHandler, fragmentLimit).18 When an audio chunk arrives, the Fragment Handler extracts the data and constructs a TRITONSERVER\_InferenceRequest.32 Crucially, rather than sending this request over localhost via gRPC, the Bridge utilizes Triton's In-Process C API (TRITONSERVER\_ServerInferAsync).32

The In-Process C API allows the Bridge application to link directly against Triton's shared libraries, passing memory pointers representing the input tensors straight into Triton's internal queues.29 This eliminates all network serialization and deserialization overhead associated with HTTP or gRPC, preserving the microsecond latency gained by using Aeron.32 The Bridge application registers a callback function; when the deep learning models finish processing the translation, Triton invokes the callback with the output tensors.9 The Bridge then packages these tensors into an UnsafeBuffer and publishes them to the outbound Aeron channel, destined for the Quarkus application.18

**Managing State: Decoupled Models and Sequence Batching**

Speech-to-speech translation pipelines process streaming data, meaning the inference execution is highly stateful and asymmetric. An incoming stream of continuous 20-millisecond audio chunks will not yield a 1:1 ratio of input requests to output responses.9

To accommodate this, the Triton model repository must be configured to use **Decoupled Transactions**. In the model's config.pbtxt file, the property model\_transaction\_policy { decoupled: True } must be set.9 A decoupled backend is permitted to send responses out-of-order relative to the execution batches, and most importantly, it can send zero responses or multiple responses for a single request.9

When the Bridge application feeds a 20ms audio chunk into Triton, the Automatic Speech Recognition (ASR) model may not yet have sufficient phonetic data to confidently predict a word or token.10 In this scenario, the decoupled ASR model yields zero responses and returns control to the caller thread, waiting for the next chunk.10 As subsequent chunks arrive and context is built, the model may suddenly generate a full phrase, triggering the machine translation and text-to-speech models to fire rapidly. The decoupled TTS model will then stream multiple synthesized audio chunks back through the C API callback in rapid succession.9

Furthermore, because the audio chunks represent continuous speech, the internal hidden states of the recurrent or transformer-based neural networks must be preserved between chunks. Triton handles this via the sequence\_batching scheduler.8 The sequence batcher ensures that all individual inference requests sharing the same correlation ID (derived from the Aeron Request ID) are routed sequentially to the exact same model instance, maintaining the conversational context without requiring the client to re-transmit the entire audio history.10

**The Low-Latency AI Translation Pipeline**

The standard approach to speech translation involves a cascaded pipeline: Automatic Speech Recognition (ASR), followed by Machine Translation (MT), followed by Text-to-Speech (TTS).35 Conventional cascaded systems operate in an offline, "Store-and-Process" manner, waiting for an end-of-utterance (EOU) silence marker before processing the entire sentence.3 This sequential batching creates an  latency scale, typically adding 3 to 5 seconds of delay, which fundamentally breaks real-time conversational interactivity.35

To achieve a conversational latency budget—targeting \<700 ms for ASR, \<500 ms for MT, and \<800 ms for TTS, culminating in a sub-two-second round trip—the system must transition to a "Stream-and-Compute" architecture.3 The pipeline must act as a bucket brigade, passing partial tokens along before the complete sentence is fully articulated.3

**Multichannel Audio and Continuous Speech Separation**

In a multi-party video call, participants frequently interrupt or speak over one another. A single-channel audio stream containing overlapping speakers drastically reduces ASR accuracy.40 To resolve this, the system leverages the multichannel audio capabilities of the client endpoints.

Before feeding audio into the primary ASR model, the pipeline may employ Continuous Speech Separation (CSS) algorithms.40 CSS systems utilize deep learning-based time-frequency mask estimation and spatial beamforming to decompose continuous multi-microphone audio into parallel, non-overlapping streams.40 Each separated speaker is assigned a unique session identifier within the pipeline, ensuring that the ASR engine processes clean, isolated audio features.40

**Streaming ASR and Wait-k Machine Translation**

The ASR model (e.g., a streaming variant of Whisper or a Conformer architecture) processes the 20-50ms audio chunks in real-time, emitting partial textual transcriptions as probability thresholds are met.43

The primary bottleneck in streaming translation is the Machine Translation (MT) phase. Conventional MT relies on encoder-decoder architectures with bidirectional cross-attention mechanisms that require the entire input sentence to generate a coherent translation.36 To facilitate streaming, the system employs Simultaneous Machine Translation (SMT) utilizing a "Wait-k" policy.3

In a Wait-k framework, the MT model is configured to begin predicting and emitting target language tokens after observing exactly  source language tokens.3 If , the model reads three words from the ASR output and immediately translates them, outputting the result before the speaker finishes the sentence.3 While Wait-k policies can introduce slight grammatical errors—particularly when translating between languages with disparate subject-verb-object structures—they prevent the pipeline from stalling, feeding a continuous stream of text to the synthesis engine.3

**Latency-Aware Text-to-Speech**

The final stage of the cascaded pipeline is the TTS vocoder. Modern latency-aware TTS systems reject the conventional paradigm of waiting for a full text sequence to generate a complete mel-spectrogram.38 Instead, they utilize incremental prefix-to-prefix architectures, processing textual tokens as they arrive and synthesizing the corresponding waveform chunks.38

By deploying highly optimized models, such as Tacotron2 paired with a WaveGlow vocoder, and compiling them using NVIDIA TensorRT within the Triton environment, the system can achieve a Real-Time Factor (RTF) significantly below 1.0.47 An RTF of \<1.0 indicates that the hardware synthesizes one second of audio in less than one second of computational time, ensuring that the playback buffer on the client side is never starved.38

**The Evolution Toward End-to-End Models**

While cascaded pipelines allow for modular optimization, the compounding latency of network hops and sequential processing stages presents a hard architectural floor.35 The state-of-the-art in speech translation is shifting toward direct, end-to-end Sequence-to-Sequence (Seq2Seq) models, such as Translatotron 2 or SeamlessM4T.35

These architectures ingest the source audio spectrogram and directly predict the target audio spectrogram and phonemes, entirely circumventing the intermediate text representation.35 A streaming implementation of these models, utilizing parallelized encoder and autoregressive decoder threads within Triton, fundamentally eliminates the cascading errors inherent in ASR-MT-TTS pipelines and allows for the preservation of the speaker's original vocal intonation and cadence.45 While harder to train and fine-tune, deploying an end-to-end model via Triton's PyTorch or TensorRT backends represents the optimal long-term strategy for achieving near-instantaneous translation.29

**Client-Side Presentation: Synchronizing WebRTC and WebSockets**

The architectural mandate specifies a bifurcated media transport strategy: video is streamed peer-to-peer using WebRTC over TURN servers, while audio is sent to the server via WebSockets for AI manipulation.4 This split design drastically conserves server bandwidth and prevents the high-definition video frames from congesting the inference hardware. However, it introduces a severe technical complication: the loss of native audio-video synchronization (lip-sync).

**The Mechanics of Asynchronous Media Drift**

Human perception places strict bounds on acceptable audio-video synchronization. According to the ITU-R BT.1359 standard, viewers begin to detect desynchronization when audio leads video by just +45 milliseconds, or when audio lags video by -125 milliseconds.4 For conversational applications, these tolerances tighten to as low as 15-45 ms.4

WebRTC natively guarantees lip-sync by heavily coupling the media streams. It encapsulates both video and audio in the Real-time Transport Protocol (RTP) and utilizes the RTP Control Protocol (RTCP) to transmit synchronization metadata.4 Every few seconds, WebRTC issues RTCP Sender Reports containing absolute Network Time Protocol (NTP) timestamps alongside relative RTP timestamps.51 The receiving browser's internal jitter buffer (NetEQ) utilizes this mapping to align the playback, dynamically accelerating or decelerating the audio to slave the video frames to the audio clock.4

By transmitting the audio over WebSockets instead of WebRTC, this internal mechanism is completely bypassed.4 WebSockets run over TCP, providing reliable, ordered bytes, but treating the payload as entirely opaque.4 TCP also suffers from Head-of-Line (HoL) blocking; if a packet is lost, all subsequent delivery is paused until retransmission occurs, causing unpredictable latency spikes.1

Furthermore, the audio stream traverses the deep learning pipeline, encountering variable inference latency. The TTS module might require 200ms to return the audio, while the WebRTC video frame traverses the peer-to-peer connection in 30ms.4 Consequently, the receiving client will display the user's lips moving hundreds of milliseconds before the translated audio arrives via the WebSocket.4

**Re-engineering Synchronization at the Application Layer**

To resolve this profound desynchronization, lip-sync must be synthetically recreated at the application layer utilizing a multi-faceted approach.

**1. The Dummy Audio Anchor** WebRTC's internal player has a low tolerance for timestamp divergence. If audio and video samples arrive with a timestamp difference exceeding approximately 200ms, the engine disables NTP synchronization and plays the streams individually to minimize absolute latency.52 Because the actual audio is stripped from the WebRTC peer connection, the sending client must inject a synthetic "dummy" audio track into the WebRTC stream.53 This track, typically an inaudible 48kHz sine wave generated via the Web Audio API's AudioContext.createOscillator(), acts as an anchor.54 It ensures that the WebRTC engine continues generating continuous RTCP Sender Reports, maintaining the absolute NTP wall-clock timing for the video frames.51

**2. Presentation Timestamps via WebSocket Headers** As the sending client records real audio chunks to transmit over the WebSocket to Quarkus, it must explicitly tag each chunk with a highly precise presentation timestamp.4 This timestamp records the exact millisecond the audio was captured, relative to the Performance.now() clock or the absolute system time.55

The Quarkus and Aeron infrastructure must treat this metadata as immutable, passing it cleanly into the Triton C-API bridge and through the inference pipeline.4 When the TTS model generates the translated audio, the pipeline calculates a new offset timestamp based on the original capture time, the duration of the processed speech, and the inferred delay.4

**3. Client-Side Jitter Buffering and WebCodecs Alignment** When the receiving client obtains the synthesized audio via the WebSocket, it cannot simply pipe it to an HTML \<audio\> element, as it will drift from the WebRTC video element.4

Instead, the client application must implement a custom jitter buffer and playback engine using the WebCodecs API and the Web Audio API's AudioWorklet.55

  - The raw WebRTC video stream is decoded frame-by-frame using WebCodecs.56
  - The incoming translated audio chunks from the WebSocket are pushed into a client-side JavaScript jitter buffer.4
  - The application reads the NTP timestamps of the decoded video frames and compares them to the custom presentation timestamps embedded in the WebSocket audio payloads.51
  - Recognizing that the video will arrive faster than the AI-processed audio, the application intentionally delays painting the video frames to an HTML \<canvas\> element.4
  - The AudioWorklet determines the exact hardware output latency and triggers the \<canvas\> to render the specific video frame only when its timestamp perfectly aligns with the audio chunk currently being played through the user's speakers.4

This "audio-master" synchronization strategy prioritizes continuous, artifact-free audio delivery—as human perception is highly intolerant of audio dropouts—while gracefully delaying the video to maintain the illusion of real-time translation.4

**Comprehensive Project Plan and Implementation Phasing**

Executing a distributed architecture of this complexity requires a rigorous, phased implementation methodology. The deployment spans edge web development, low-level systems programming, deep learning orchestration, and front-end media synchronization. The following project plan outlines the critical path to a production-ready application.

**Phase 1: Ingress/Egress and Transport Plumbing (Months 1-2)**

**Objective**: Establish the high-performance boundaries and the internal zero-copy messaging backbone, verifying microsecond transit times before introducing heavy AI workloads.

  

|                                    |                                                                                                                                                                |                                                                                                                   |
| :--------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------: | :---------------------------------------------------------------------------------------------------------------: |
| **Milestone**                  | **Technical Description**                                                                                                                                  | **Deliverables & Verification**                                                                               |
| **1.1 Edge WebSocket Gateway** | Implement the Quarkus application utilizing quarkus-websockets-next.15 Configure reactive event loops and non-blocking I/O.                                    | A load-tested endpoint capable of accepting 10,000 concurrent binary streams with \<1ms application overhead.16 |
| **1.2 Memory Management**      | Develop custom Direct Byte Buffer pooling within Quarkus to ingest binary audio payloads without triggering Java heap garbage collection.16                    | Profiling reports demonstrating zero major GC pauses during sustained peak load.11                                |
| **1.3 Aeron Media Driver**     | Deploy the standalone Aeron Media Driver on the target Linux infrastructure. Configure aeron:ipc memory-mapped files (/dev/shm) and tune OS page sizes.24      | Media driver operational with AeronStat confirming healthy ring buffer allocation.57                              |
| **1.4 Aeron Java Integration** | Integrate the Aeron Java API into Quarkus. Construct the Publication logic to wrap binary data and custom Request ID headers into SBE-encoded UnsafeBuffers.18 | Successful end-to-end echo test: Data flows from WebSocket  Quarkus  Aeron  Quarkus  WebSocket.18                 |

**Phase 2: Inference Engine Orchestration (Months 3-4)**

**Objective**: Bridge the Aeron transport layer to the GPU-accelerated Triton Inference Server and establish asynchronous streaming capabilities.

  

|                                    |                                                                                                                                                                |                                                                                                         |
| :--------------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------: |
| **Milestone**                  | **Technical Description**                                                                                                                                  | **Deliverables & Verification**                                                                     |
| **2.1 The C-API Bridge**       | Develop a highly optimized C++ or Java proxy service. Implement an Aeron Subscription utilizing a FragmentHandler to pull SBE chunks from the log buffer.7     | The proxy successfully reads millions of messages per second with microsecond latency.33                |
| **2.2 Triton In-Process Link** | Connect the Bridge directly to Triton using TRITONSERVER\_ServerInferAsync. Bypass gRPC/HTTP entirely to write tensor data directly to Triton's queues.32    | Tensor validation scripts confirming data integrity without network serialization overhead.32           |
| **2.3 Decoupled Batching**     | Configure Triton model repositories (config.pbtxt). Enable decoupled: True and implement the sequence batcher to maintain conversational state across chunks.9 | Triton successfully processes continuous streams and yields out-of-order, multiple-response callbacks.9 |

**Phase 3: AI Pipeline Integration (Months 5-6)**

**Objective**: Deploy the deep learning models and configure the internal orchestration to meet the strict T-TAT latency budgets.

  

|                                 |                                                                                                                                                                      |                                                                                                |
| :-----------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------------------: | :--------------------------------------------------------------------------------------------: |
| **Milestone**               | **Technical Description**                                                                                                                                        | **Deliverables & Verification**                                                            |
| **3.1 Model Deployment**    | Load streaming ASR, Simultaneous MT, and latency-aware TTS models (or an end-to-end Translatotron architecture) into Triton.29                                       | Models execute successfully within the Triton backend environments (TensorRT/PyTorch).8        |
| **3.2 Pipeline Scripting**  | Implement Business Logic Scripting (BLS) to chain the output tensors of ASR directly into the MT engine, and MT into TTS, avoiding data round-trips to the Bridge.29 | Entire speech-to-speech inference executes as a single, unified request lifecycle.59           |
| **3.3 Wait-k Optimization** | Tune the machine translation model for Wait-k execution to force partial text outputs, mitigating  sequence delays.3                                                 | Total inference latency from audio ingestion to audio output consistently measures \<500ms.1 |

**Phase 4: Client-Side Presentation and Hardening (Months 7-8)**

**Objective**: Finalize the user experience by resolving the inherent media drift and hardening the network layer against degradation.

  

|                                         |                                                                                                                                                     |                                                                                                                |
| :-------------------------------------: | :-------------------------------------------------------------------------------------------------------------------------------------------------: | :------------------------------------------------------------------------------------------------------------: |
| **Milestone**                       | **Technical Description**                                                                                                                       | **Deliverables & Verification**                                                                            |
| **4.1 WebRTC P2P Video**            | Establish direct client-to-client video connections using STUN/TURN traversal. Inject the 48kHz dummy oscillator to maintain RTCP Sender Reports.53 | Stable, high-definition video transmission with absolute NTP timestamp generation.51                           |
| **4.2 Application Jitter Buffer**   | Develop the JavaScript AudioWorklet and WebCodecs engine. Parse the custom presentation timestamps from the WebSocket audio payloads.4              | Video frames render to the HTML \<canvas\> in perfect synchronization with the synthesized audio.4         |
| **4.3 Network Degradation Testing** | Simulate packet loss and high-jitter environments. Tune WebSocket reconnection logic and Aeron NAK (Negative Acknowledgement) windowing.4           | Application maintains conversational fluidity without catastrophic crashing during 10% packet loss scenarios.4 |

**Conclusions**

Constructing a multichannel speech-to-speech video application demands an architecture that ruthlessly eliminates computational and transport delays at every node. By rejecting standard HTTP REST polling in favor of persistent, binary-framed WebSockets via Quarkus, the system removes critical edge overhead.2 Translating the conceptual data pipelines into Aeron's Channels, Stream IDs, and Session IDs provides a deterministically fast, brokerless IPC backbone that outperforms traditional disk-backed streaming systems.13

Crucially, the success of the machine learning inference relies on bypassing standard network protocols. The deployment of an In-Process C-API Bridge allows the low-latency Aeron streams to inject tensor data directly into Triton Inference Server's memory space.32 By enabling decoupled, sequence-batched model execution, Triton can handle the asymmetric reality of streaming audio, pushing continuous synthesized frames back out through the pipeline.9

Finally, the architectural decision to bifurcate the video over WebRTC while processing audio through the server fundamentally breaks native media synchronization.4 This requires sophisticated client-side engineering, utilizing dummy audio anchors to preserve NTP clocks, and WebCodecs paired with AudioWorklet processing to synthetically align the real-time video with the latency-incurred translated audio.4 Adherence to this topology and the proposed multi-phase implementation plan will yield a highly scalable platform capable of delivering true, real-time cross-lingual conversational experiences.

**Works cited**

1.  How is WebRTC Used for Bi-Directional Voice and Video Streaming in AI Agents?, accessed on February 27, 2026, <https://getstream.io/blog/webrtc-ai-voice-video/>
2.  Building Real-Time Audio APIs Using WebSockets at Scale: Engineering Production-Grade Voice Infrastructure - Fonadalabs, accessed on February 27, 2026, <https://fonadalabs.ai/blog/building-real-time-audio-apis-using-websockets-at-scale-engineering-production-grade-voice-infrastructure>
3.  Optimizing Latency in Real-Time Speech Translation Pipelines - WeblineGlobal, accessed on February 27, 2026, <https://www.weblineglobal.com/blog/optimizing-real-time-speech-translation-latency/>
4.  WebRTC vs. WebSocket: Which Keeps Audio and Video in Sync for AI? - GetStream.io, accessed on February 27, 2026, <https://getstream.io/blog/webrtc-websocket-av-sync/>
5.  Stream audio from client to server to client using WebSocket - Stack Overflow, accessed on February 27, 2026, <https://stackoverflow.com/questions/63103293/stream-audio-from-client-to-server-to-client-using-websocket>
6.  WebSockets Next reference guide - Quarkus, accessed on February 27, 2026, <https://quarkus.io/guides/websockets-next-reference>
7.  Channels, Streams and Sessions - Documentation - Aeron, accessed on February 27, 2026, <https://aeron.io/docs/aeron/aeron-channel-stream-session/>
8.  The Triton Inference Server provides an optimized cloud and edge inferencing solution. - GitHub, accessed on February 27, 2026, <https://github.com/triton-inference-server/server>
9.  Decoupled Backends and Models — NVIDIA Triton Inference Server, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/user_guide/decoupled_models.html>
10. Optimizing ASR Streaming: Implementing Continuous Audio Processing for Uninterrupted Response · Issue \#6865 · triton-inference-server/server - GitHub, accessed on February 27, 2026, <https://github.com/triton-inference-server/server/issues/6865>
11. Quarkus Performance, accessed on February 27, 2026, <https://quarkus.io/performance/>
12. Quarkus - Supersonic Subatomic Java, accessed on February 27, 2026, <https://quarkus.io/>
13. Building Low-Latency Messaging with Aeron in Java | by Ryan McCauley - Medium, accessed on February 27, 2026, <https://medium.com/version-1/building-low-latency-messaging-with-aeron-in-java-cfe1d841d5d4>
14. Stream audio over websocket with low latency and no interruption - Stack Overflow, accessed on February 27, 2026, <https://stackoverflow.com/questions/55975895/stream-audio-over-websocket-with-low-latency-and-no-interruption>
15. Getting started with WebSockets Next - Quarkus, accessed on February 27, 2026, <https://quarkus.io/guides/websockets-next-tutorial>
16. Quarkus websocket performance degradation compared to netty · Issue \#36342 - GitHub, accessed on February 27, 2026, <https://github.com/quarkusio/quarkus/issues/36342>
17. Towards a new websocket model · quarkusio quarkus · Discussion \#38473 - GitHub, accessed on February 27, 2026, <https://github.com/quarkusio/quarkus/discussions/38473>
18. Java Programming Guide · aeron-io/aeron Wiki - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io/aeron/wiki/Java-Programming-Guide>
19. quarkus web socket drops connection after 1 minute - Stack Overflow, accessed on February 27, 2026, <https://stackoverflow.com/questions/78631458/quarkus-web-socket-drops-connection-after-1-minute>
20. High-Performance Messaging with Aeron: A Practical Guide | by Kostiantyn Ivanov | Medium, accessed on February 27, 2026, <https://medium.com/@svosh2/the-aeron-e101d54262f4>
21. Aeron — low latency transport protocol | by Alexey Pirogov - Medium, accessed on February 27, 2026, <https://medium.com/@pirogov.alexey/aeron-low-latency-transport-protocol-9493f8d504e8>
22. Aeron - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io>
23. Channels, Streams & Sessions - The Aeron Files, accessed on February 27, 2026, <https://theaeronfiles.com/aeron-transport/channels-streams-sessions/>
24. Fastest way to get data from one process to another: Aeron Transport | by Apurva Singh, accessed on February 27, 2026, <https://medium.com/@apusingh1967/fastest-way-to-get-data-from-one-process-to-another-aeron-transport-3595249c9ffd>
25. Session and Stream terms clarification · Issue \#537 · aeron-io/aeron - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io/aeron/issues/537>
26. Client/server architecture · Issue \#652 · aeron-io/aeron - GitHub, accessed on February 27, 2026, <https://github.com/real-logic/aeron/issues/652>
27. aeron-io/aeron: Efficient reliable UDP unicast, UDP multicast, and IPC message transport - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io/aeron>
28. Basic Aeron Sample - Documentation, accessed on February 27, 2026, <https://aeron.io/docs/aeron/basic-sample/>
29. NVIDIA Triton Inference Server, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/index.html>
30. Inference Protocols and APIs — NVIDIA Triton Inference Server - NVIDIA Documentation, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/customization_guide/inference_protocols.html>
31. Triton Inference Server Backend - NVIDIA Documentation, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/backend/README.html>
32. C API Description — NVIDIA Triton Inference Server, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/customization_guide/inprocess_c_api.html>
33. Triton Client Libraries and Examples — NVIDIA Triton Inference Server, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/client/README.html>
34. Client Examples — NVIDIA Triton Inference Server 2.0.0 documentation, accessed on February 27, 2026, <https://docs.nvidia.com/deeplearning/triton-inference-server/archives/triton_inference_server_1140/user-guide/docs/client_example.html>
35. Real End-to-End Speech-to-Speech Translation is among us - Dr. Claudio Fantinuoli, accessed on February 27, 2026, <https://www.claudiofantinuoli.org/2025/11/28/real-end-to-end-speech-to-speech-translation-is-among-us/>
36. Overcoming Latency Bottlenecks in On-Device Speech Translation: A Cascaded Approach with Alignment-Based Streaming MT - arXiv, accessed on February 27, 2026, <https://arxiv.org/html/2508.13358v1>
37. Building a Low Latency Speech-To-Speech RAG Bot — ACE Agent - NVIDIA Documentation, accessed on February 27, 2026, <https://docs.nvidia.com/ace/ace-agent/4.1/tutorials/build-bot-rag.html>
38. Latency-Aware TTS Pipeline - Emergent Mind, accessed on February 27, 2026, <https://www.emergentmind.com/topics/latency-aware-tts-pipeline>
39. How to Build a Real-Time Voice Translator with Open-Source AI - GMI Cloud, accessed on February 27, 2026, <https://www.gmicloud.ai/blog/how-to-build-a-real-time-voice-translator-with-open-source-ai>
40. Continuous Speech Separation - Emergent Mind, accessed on February 27, 2026, <https://www.emergentmind.com/topics/continuous-speech-separation>
41. Advances in Speech Separation: Techniques, Challenges, and Future Trends - arXiv.org, accessed on February 27, 2026, <https://arxiv.org/html/2508.10830v1>
42. Real-Time Speech to Text: Live Transcription Guide - AssemblyAI, accessed on February 27, 2026, <https://www.assemblyai.com/blog/real-time-speech-to-text>
43. Engineering a Multi-Channel AI Voice Agent with Twilio, Whisper, ElevenLabs, and Supabase | by Nemon | Medium, accessed on February 27, 2026, <https://medium.com/@anemonerozman76/engineering-a-multi-channel-ai-voice-agent-with-twilio-whisper-elevenlabs-and-supabase-5d12950a337c>
44. ElevenLabs Scribe v2 Realtime: The Most Accurate Real-Time Speech-to-Text Model, accessed on February 27, 2026, <https://vatsalshah.in/blog/elevenlabs-scribe-v2-realtime-speech-to-text>
45. SimulTron: On-Device Simultaneous Speech to Speech Translation - arXiv, accessed on February 27, 2026, <https://arxiv.org/html/2406.02133v1>
46. Latency-Aware TTS Pipeline - Emergent Mind, accessed on February 27, 2026, <https://www.emergentmind.com/topics/latency-aware-text-to-speech-tts-pipeline>
47. Getting a Real Time Factor Over 60 for Text-To-Speech Services Using NVIDIA Riva, accessed on February 27, 2026, <https://developer.nvidia.com/blog/getting-real-time-factor-over-60-for-text-to-speech-using-riva/>
48. Talk 30 Languages in \<1 sec: an Open-Source Real-Time Speech Translator I built single handedly | by Mehmet Enes Onuş | Medium, accessed on February 27, 2026, <https://medium.com/@menes.onus/talk-30-languages-in-1-sec-an-open-source-real-time-speech-translator-i-built-single-handedly-5f461d4cf45e>
49. Translatotron 2 Speech-to-Speech Translation Architecture - GeeksforGeeks, accessed on February 27, 2026, <https://www.geeksforgeeks.org/deep-learning/translatotron-2-speech-to-speech-translation-architecture/>
50. Fighting WebRTC Audio Sync Issues | StreetJelly.com Blog, accessed on February 27, 2026, <http://blog.streetjelly.com/2017/03/fighting-webrtc-audio-sync-issues/>
51. Lip synchronization and WebRTC applications - BlogGeek.me, accessed on February 27, 2026, <https://bloggeek.me/lip-synchronization-webrtc/>
52. How does audio and video in a webrtc peerconnection stay in sync? - Stack Overflow, accessed on February 27, 2026, <https://stackoverflow.com/questions/66479379/how-does-audio-and-video-in-a-webrtc-peerconnection-stay-in-sync>
53. How to stream audio from browser to WebRTC native C++ application - Stack Overflow, accessed on February 27, 2026, <https://stackoverflow.com/questions/23449666/how-to-stream-audio-from-browser-to-webrtc-native-c-application>
54. WebRTC video/audio streams out of sync (MediaStream -> MediaRecorder -> MediaSource -> Video Element) - Stack Overflow, accessed on February 27, 2026, <https://stackoverflow.com/questions/52134781/webrtc-video-audio-streams-out-of-sync-mediastream-mediarecorder-mediasou>
55. Synchronize audio/video with data in WebRTC · Issue \#133 · w3c/strategy - GitHub, accessed on February 27, 2026, <https://github.com/w3c/strategy/issues/133>
56. Synchronize audio and video playback on the web | Articles - web.dev, accessed on February 27, 2026, <https://web.dev/articles/audio-output-latency>
57. Monitoring and Debugging · aeron-io/aeron Wiki - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io/aeron/wiki/Monitoring-and-Debugging>
58. Cpp Programming Guide · aeron-io/aeron Wiki - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io/aeron/wiki/Cpp-Programming-Guide>
59. Serving ML Model Pipelines on NVIDIA Triton Inference Server with Ensemble Models, accessed on February 27, 2026, <https://developer.nvidia.com/blog/serving-ml-model-pipelines-on-nvidia-triton-inference-server-with-ensemble-models/>
60. Transport Protocol Specification · aeron-io/aeron Wiki - GitHub, accessed on February 27, 2026, <https://github.com/aeron-io/aeron/wiki/Transport-Protocol-S...>

