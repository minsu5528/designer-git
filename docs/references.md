# 기존 연구 및 관련 자료

---

## 1. 바이너리 파일 버전관리 관련 선행 연구

[기술보고서: "The rsync algorithm", TR-CS-96-05, Australian National University, 1996]

이 기술보고서는 Andrew Tridgell과 Paul Mackerras가 저대역폭 환경에서 두 파일 간 변경된 부분만 전송하는 알고리즘을 제안한 연구다. 파일을 고정 크기 블록으로 나누고 각 블록의 rolling checksum을 계산한 뒤, 두 버전 간 동일 블록은 건너뛰고 변경된 블록만 전송한다. 슬라이딩 윈도우 방식으로 윈도우가 한 칸 이동할 때 나가는 바이트의 기여분을 빼고 새 바이트를 더하는 O(1) 업데이트를 구현하여, 전체를 재계산하지 않고도 해시값을 유지한다.

그러나 이 알고리즘은 두 머신 간 파일 동기화에 특화된 도구로, 변경 이력 저장, 특정 시점 복원(checkout), 커밋 로그 조회 등의 버전관리 기능이 없다. designer_git은 이 delta 추출 원리를 계승하되, 커밋 체인·SHA-256 검증·체크아웃을 포함한 완전한 버전관리 레이어를 추가하여 단순 동기화 도구와 차별화된다.

---

[기술문서: "FBX binary file format specification", Blender Foundation, 2013]

이 문서는 Alexander Gessler가 역공학으로 분석하고 Campbell Barton이 검토하여 Blender Foundation이 공개 도메인으로 배포한 Binary FBX 포맷 비공식 명세다. Autodesk가 바이너리 FBX 포맷을 공식 문서화하지 않아 작성된 자료로, 불완전(incomplete)하다고 명시되어 있다.

파일 헤더는 27바이트로 구성되며 "Kaydara FBX Binary" 매직 넘버로 시작한다. 이후 Node Record가 재귀적으로 중첩되는 구조로, 각 Node Record는 EndOffset(다음 레코드까지의 거리) / NumProperties / PropertyListLen / Name / Properties / Children 필드로 구성된다. 정점 데이터는 Geometry > Vertices 노드에 float 배열로 연속 저장되어 있다. 이 연속 저장 구조가 정점 1개 수정 시 해당 float 값들이 위치한 바이트 구간만 국소적으로 변경된다는 delta 효율 근거의 기술적 토대가 된다.

단, 이 명세는 Autodesk 공식 문서가 아닌 역공학 기반이며 불완전하다. Maya와 Blender가 각각 FBX를 저장할 때 내부 직렬화 방식이 다를 수 있으며, 이로 인한 실제 delta 효율 차이는 5/30 벤치마크에서 실측으로 검증 예정이다.

---

## 2. CDC / Rolling Hash 성능 비교 관련 문헌

[논문: "FastCDC: A Fast and Efficient Content-Defined Chunking Approach for Data Deduplication", USENIX ATC 2016]

Wen Xia 외 다수가 저술하여 2016 USENIX Annual Technical Conference(pp. 101–114)에서 발표한 정식 학술 논문이다. Content-Defined Chunking(CDC)이 데이터 중복 제거 시스템에서 15년 이상 핵심 역할을 해왔음을 배경으로, 기존 Rabin 기반 CDC의 문제점과 개선 방향을 제시한다.

기존 Rabin CDC는 바이트 단위로 rolling hash를 계산하고 판별하는 과정에서 CPU 오버헤드가 크다. 이 논문은 (1) hash judgment 단순화, (2) 최소 청크 크기 이하 구간의 cut-point skipping, (3) 청크 크기 분포 정규화 세 가지 기법을 결합한 FastCDC를 제안한다. 8KB·16KB 등 다양한 평균 청크 크기별 중복 제거율과 처리 속도를 실측 비교 데이터(표 형식)로 제시하며, Rabin 기반 CDC(RC), Gear 기반 CDC(GC), FastCDC(FC) 세 가지를 직접 비교한다.

designer_git이 Rabin 기반 CDC를 선택한 이유, 블록 크기 16KB 설정 근거, 그리고 향후 성능 최적화 방향을 설명할 때 비교 자료로 활용한다. designer_git은 중복 제거가 아닌 버전관리 맥락의 delta 추출이 목적이므로 적용 목적이 다르다는 점을 발표 시 명확히 해야 한다.

---

[논문 (arXiv 프리프린트): "A Thorough Investigation of Content-Defined Chunking Algorithms for Data Deduplication", arXiv:2409.06066, 2024]

IEEE 제출 중인 CDC 알고리즘 종합 비교 서베이 논문이다. Rabin, FastCDC, AE, LMC 등 주요 CDC 알고리즘들을 hashing 기반과 hash-less 방식으로 분류하고 성능을 체계적으로 비교한다. Rabin fingerprinting이 CDC의 최초 주요 구현임을 정리하고, 청크 크기 분산·연산 오버헤드·바이트 시프팅 문제 등 알고리즘별 한계를 다룬다.

⚠ 정식 출판 전 arXiv 프리프린트 상태로, 동료 심사(peer review)가 완료되지 않았다. 인용 시 반드시 "arXiv preprint, 2024, 정식 출판 미완료"로 명시해야 한다. 발표 Q&A에서 이 자료를 언급할 경우 프리프린트임을 인지하고 있어야 한다.

---

[논문: "Accelerating Content-Defined-Chunking Based Data Deduplication by Exploiting Parallelism", Future Generation Computer Systems, 2019]

Elsevier 저널 Future Generation Computer Systems(vol. 96, pp. 142–153)에 게재된 정식 논문이다. Rabin, Adler, Gear 세 가지 CDC 알고리즘을 동일 조건에서 실측 비교한 데이터를 포함한다. quad-core Intel i7-4770 프로세서 기준으로 CDC 병렬화를 통해 처리량을 3~4배 향상시키면서 중복 제거율은 0.02% 미만 감소함을 실험으로 증명했다. CDC 연산이 deduplication 파이프라인의 병목임을 정량적으로 보여준다.

향후 멀티스레딩 최적화 적용 시 예상 효과(벤치마크 목표치 미달 시 플랜 B)를 뒷받침하는 근거 자료로 활용할 수 있다.

---

## 3. Git LFS 한계 관련 자료

[공식문서: "Git Large File Storage (LFS)", GitLab Documentation, 2024]

GitLab 공식 문서로, Git이 바이너리 파일을 처리하는 구조적 한계와 Git LFS의 동작 원리를 설명한다. Git은 텍스트 파일과 달리 바이너리 파일의 변경 내용을 diff로 추출하지 못하며, 변경이 발생하면 저장소 내 파일 전체를 교체해야 한다. 이로 인해 대용량 파일의 반복 변경은 저장소 크기를 빠르게 증가시키고 clone, fetch, pull 속도를 저하시킨다. Git LFS는 대용량 파일을 외부 저장소에 두고 저장소에는 포인터만 남기는 방식으로 저장소 크기 문제는 완화하지만, 파일 수정 시 변경분만 저장하는 것이 아니라 전체 파일을 새 버전으로 저장하므로 스토리지 폭발 문제의 근본 원인인 "바이너리 delta 추출 불가" 문제는 해결하지 못한다.

---

[공식문서: "Work with large files in your Git repo", Microsoft Azure DevOps Documentation, 2024]

Microsoft Azure Repos 공식 문서로, 엔터프라이즈 환경에서 Git LFS 사용 시 발생하는 실질적 제약을 공식적으로 명시한다. 바이너리 에셋 작업 전 항상 최신본을 pull해야 하는 협업 제약, SSH 미지원 등의 한계가 문서화되어 있다. 소스 파일과 바이너리 의존성을 별도로 관리하도록 권고하는 등 Git LFS가 바이너리 버전관리의 완전한 해결책이 아님을 공식적으로 인정한다.

---

## 참고문헌

- Tridgell, A., Mackerras, P., "The rsync algorithm", Technical Report TR-CS-96-05, Department of Computer Science, Australian National University, 1996. https://www.samba.org/rsync/tech_report/
- Gessler, A., "FBX binary file format specification", Blender Foundation, published as public domain information, 2013. https://code.blender.org/2013/08/fbx-binary-file-format-specification/
- Xia, W., Zhou, Y., Jiang, H., Feng, D., Hua, Y., Hu, Y., Liu, Q., Zhang, Y., "FastCDC: A Fast and Efficient Content-Defined Chunking Approach for Data Deduplication", in Proceedings of 2016 USENIX Annual Technical Conference (USENIX ATC '16), Denver, CO, pp. 101–114, 2016.
- (2024). "A Thorough Investigation of Content-Defined Chunking Algorithms for Data Deduplication", arXiv preprint arXiv:2409.06066. (정식 출판 미완료)
- Luo, T., et al., "Accelerating Content-Defined-Chunking Based Data Deduplication by Exploiting Parallelism", Future Generation Computer Systems, vol. 96, pp. 142–153, Elsevier, 2019.
- GitLab Documentation, "Git Large File Storage (LFS)", 2024. https://docs.gitlab.com/topics/git/lfs
- Microsoft Learn / Azure Repos, "Work with large files in your Git repo", 2024. https://learn.microsoft.com/en-us/azure/devops/repos/git/manage-large-files
