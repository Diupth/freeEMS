# PROMPT: Phân tích và xây dựng giao diện phần mềm mô phỏng điện từ tương tự CST Studio Suite

## 1. Vai trò

Bạn là chuyên gia thiết kế phần mềm kỹ thuật (Engineering Software), có
kinh nghiệm về: - Phần mềm mô phỏng điện từ và siêu cao tần
(RF/Microwave/EM Simulation). - Thiết kế giao diện CAD/CAE có mô hình
3D. - Kiến trúc phần mềm đa nền tảng cho Linux và macOS. - Thiết kế
UI/UX cho ứng dụng kỹ thuật có nhiều cửa sổ, bảng thuộc tính và công cụ
phân tích dữ liệu.

Hãy phân tích giao diện CST Studio Suite được mô tả trong prompt này và
đề xuất/triển khai một giao diện phần mềm mô phỏng điện từ có cách tổ
chức chức năng tương tự. Đây là phần mềm độc lập, không sao chép logo,
nhận diện thương hiệu, biểu tượng độc quyền hoặc tài nguyên đồ họa của
CST.

## 2. Mục tiêu tổng thể

Xây dựng giao diện cho một phần mềm mô phỏng điện từ 3D, phục vụ thiết
kế và phân tích các hệ thống RF, microwave, antenna, transmission line,
filter, coupler, waveguide, PCB và các cấu trúc điện từ khác.

Giao diện cần: - Có bố cục chuyên nghiệp theo phong cách phần mềm
CAD/CAE. - Hỗ trợ làm việc với mô hình hình học 3D và kết quả mô
phỏng. - Có cây quản lý đối tượng ở bên trái. - Có vùng hiển thị mô hình
3D và kết quả ở trung tâm. - Có vùng schematic/circuit ở bên phải. - Có
bảng tham số ở phía dưới. - Có khu vực thông báo và tiến trình mô
phỏng. - Có thanh công cụ dạng Ribbon thay đổi theo ngữ cảnh. - Có khả
năng mở rộng thêm solver, công cụ hậu xử lý và các module kỹ thuật. -
Chạy được trên Linux và macOS.

## 3. Bố cục giao diện chính

Thiết kế cửa sổ chính theo bố cục nhiều vùng có thể thay đổi kích thước
(dockable/resizable panels).

### 3.1. Menu Bar -- Thanh menu chính

Đặt ở phía trên cùng cửa sổ. Các menu dự kiến:

-   **File:** New Project, Open Project, Save, Save As, Import, Export,
    Recent Projects, Exit.
-   **Home:** Các thao tác cơ bản, thiết lập dự án, lựa chọn đối tượng
    và truy cập nhanh các chức năng thường dùng.
-   **Modeling:** Tạo và chỉnh sửa hình học 3D, Boolean operations,
    transform, coordinate system, material assignment.
-   **Simulation:** Chọn solver, thiết lập dải tần, nguồn kích thích,
    boundary conditions, mesh, monitors và chạy mô phỏng.
-   **Post-Processing:** Phân tích S-parameters, Smith Chart, VSWR,
    gain, radiation pattern, E-field, H-field, surface current, power
    flow và các kết quả khác.
-   **View:** Điều khiển bố cục cửa sổ, hiển thị/ẩn panel, góc nhìn, chế
    độ hiển thị và theme.
-   **Tools:** Các tiện ích, calculator, optimization, parameter sweep
    và scripting.
-   **Help:** Tài liệu hướng dẫn, ví dụ, thông tin phiên bản và hỗ trợ.

Các menu phải được tổ chức nhất quán; chức năng chưa triển khai cần được
đánh dấu rõ ràng, không tạo nút giả gây hiểu nhầm.

### 3.2. Ribbon Toolbar -- Thanh công cụ theo ngữ cảnh

Ngay dưới Menu Bar là thanh Ribbon. Các tab và nhóm công cụ thay đổi
theo tác vụ đang thực hiện.

Các tab đề xuất:

**Home** - New/Open/Save Project. - Undo/Redo. - Select, Move, Rotate,
Scale. - Fit View, Zoom, Pan, Rotate View. - Hiển thị/ẩn Navigation
Tree, Properties, Console.

**Modeling** - Create Box, Cylinder, Sphere, Cone, Polygon. - Extrude,
Sweep, Loft. - Unite, Subtract, Intersect. - Move, Rotate, Mirror,
Scale. - Assign Material. - Create/Manage Coordinate System. - Create
Port và các cấu trúc hình học phụ trợ.

**Simulation** - Solver Selection. - Frequency Range. - Boundary
Conditions. - Excitation/Ports. - Mesh Settings. - Field Monitors. -
Run, Pause, Stop Simulation. - Solver Log và Simulation Setup.

**Results** - 1D Plot. - 2D Plot. - 3D Field Plot. - E-Field, H-Field,
Surface Current. - S-Parameter. - Smith Chart. - Radiation Pattern. -
Field Calculator. - Animation. - Export Results.

**View** - Perspective/Orthographic. -
Front/Back/Top/Bottom/Left/Right. - Wireframe, Solid, Transparent. -
Show/Hide Grid, Axis, Mesh, Labels. - Layout và Theme.

Ribbon cần có biểu tượng dễ nhận biết, nhãn rõ ràng, tooltip giải thích
chức năng và trạng thái enable/disable phù hợp với ngữ cảnh.

### 3.3. Navigation Tree -- Cây quản lý dự án

Đặt ở phía trái cửa sổ. Cho phép quản lý toàn bộ cấu trúc dự án theo
dạng cây phân cấp.

Các nhóm chính:

-   **Components:** Các cụm đối tượng hình học.
-   **Groups:** Nhóm đối tượng do người dùng tạo.
-   **Materials:** Danh sách vật liệu và thuộc tính điện từ.
-   **Solids/Shapes:** Các khối hình học.
-   **Faces/Edges/Curves:** Các thành phần hình học được chọn hoặc tham
    chiếu.
-   **Coordinate Systems:** Hệ tọa độ toàn cục và cục bộ.
-   **Ports:** Waveguide Port, Discrete Port và các loại cổng khác.
-   **Boundaries:** Điều kiện biên.
-   **Sources/Excitations:** Nguồn kích thích.
-   **Lumped Elements:** Điện trở, tụ điện, cuộn cảm và phần tử tập
    trung.
-   **Monitors:** Các điểm/vùng theo dõi kết quả.
-   **Mesh:** Cấu hình và thông tin lưới tính toán.
-   **Simulation Setup:** Các thiết lập mô phỏng.
-   **1D Results:** Kết quả dạng đồ thị một chiều.
-   **2D/3D Results:** Kết quả trường và phân bố không gian.
-   **Tables:** Bảng dữ liệu kết quả.

Yêu cầu tương tác: - Mở rộng/thu gọn từng nhánh. - Nhấp đúp để mở hoặc
hiển thị đối tượng. - Menu chuột phải theo từng loại đối tượng. - Tìm
kiếm và lọc đối tượng. - Bật/tắt hiển thị đối tượng trong viewport. -
Đồng bộ lựa chọn giữa Navigation Tree và 3D Viewport. - Hiển thị biểu
tượng/trạng thái phù hợp cho từng loại node.

### 3.4. 3D Viewport -- Vùng hiển thị mô hình 3D

Đặt ở vùng trung tâm, là khu vực làm việc chính.

Chức năng: - Hiển thị mô hình hình học 3D. - Chọn đối tượng bằng
chuột. - Xoay, pan, zoom và fit mô hình. - Hiển thị vật liệu bằng màu
sắc hoặc chế độ shading. - Hiển thị wireframe, solid, transparent và
hidden-line. - Hiển thị hệ trục tọa độ, lưới tham chiếu và thước đo. -
Hỗ trợ mặt phẳng cắt (cutting plane). - Hiển thị mesh khi người dùng bật
chế độ xem lưới. - Hiển thị kết quả trường điện từ trực tiếp trên mô
hình.

Khi hiển thị kết quả mô phỏng: - Có color map biểu diễn cường độ
trường. - Có color scale/legend kèm đơn vị. - Cho phép chỉnh min/max,
linear/log scale và giới hạn dải màu. - Có thể chọn thành phần X/Y/Z
hoặc Magnitude/Phase nếu dữ liệu hỗ trợ. - Cho phép đặt mặt phẳng cắt và
thay đổi vị trí mặt cắt. - Hiển thị giá trị tại vị trí con trỏ. - Có
chức năng tạo animation theo thời gian hoặc pha nếu loại solver hỗ trợ.

Không được hiển thị số liệu mô phỏng giả như thể đó là kết quả solver
thật. Nếu dùng dữ liệu minh họa, phải ghi rõ "Demo Data" hoặc "Sample
Result".

### 3.5. Schematic View -- Vùng sơ đồ mạch

Đặt bên phải 3D Viewport hoặc cho phép chuyển thành tab riêng.

Mục đích là xây dựng và hiển thị sơ đồ mạch tương đương, hỗ trợ đồng mô
phỏng mạch và trường điện từ.

Các chức năng giao diện: - Đặt linh kiện lên vùng làm việc. - Kết nối
linh kiện bằng dây dẫn. - Di chuyển, xoay, xóa và sao chép linh kiện. -
Hiển thị tên linh kiện và giá trị tham số. - Hỗ trợ các phần tử R, L, C,
nguồn, tải, cổng và khối mô hình hóa. - Kiểm tra kết nối và phát hiện
node chưa nối. - Quản lý liên kết giữa schematic và các cổng của mô hình
3D.

Schematic cần có lưới nền, thao tác zoom/pan, snap-to-grid và menu chuột
phải. Phần mềm cần phân biệt rõ sơ đồ mạch thực sự có thể mô phỏng với
sơ đồ chỉ dùng để minh họa.

### 3.6. Parameter List -- Bảng tham số

Đặt phía dưới vùng làm việc chính, có thể dock hoặc thay đổi kích thước.

Các cột: - **Name:** Tên biến. - **Expression:** Biểu thức hoặc giá trị
khai báo. - **Value:** Giá trị đã được tính toán. - **Unit:** Đơn vị. -
**Description:** Mô tả.

Chức năng: - Thêm, sửa, xóa tham số. - Kiểm tra cú pháp biểu thức. -
Phát hiện tham chiếu biến không tồn tại và vòng lặp phụ thuộc. - Hỗ trợ
đơn vị kỹ thuật như mm, cm, m, Hz, MHz, GHz, Ohm, dB. - Cho phép sử dụng
tham số trong kích thước hình học, vật liệu và cấu hình mô phỏng. - Cập
nhật mô hình khi tham số thay đổi. - Hỗ trợ parameter sweep và tối ưu
hóa trong các phiên bản mở rộng.

Ví dụ: - `substrate_h = 1.6 mm` - `patch_w = 28 mm` -
`patch_l = 35 mm` - `feed_gap = 0.5 mm` - `f_start = 8 GHz` -
`f_stop = 12 GHz`

### 3.7. Progress và Messages

Đặt ở phía dưới hoặc dưới bên phải giao diện.

**Progress** - Hiển thị tác vụ đang chạy. - Hiển thị trạng thái
solver. - Hiển thị phần trăm hoàn thành nếu solver cung cấp. - Hiển thị
thời gian chạy và tác vụ hiện tại. - Có nút Pause/Stop nếu solver hỗ
trợ.

**Messages** - Thông báo thông tin (Info). - Cảnh báo (Warning). - Lỗi
(Error). - Thời gian phát sinh. - Tên module hoặc solver phát sinh thông
báo. - Cho phép lọc theo mức độ và tìm kiếm nội dung.

Thông báo lỗi cần có nội dung dễ hiểu và gợi ý cách khắc phục khi có
thể.

### 3.8. Status Bar

Đặt ở cạnh dưới cùng cửa sổ.

Hiển thị: - Trạng thái ứng dụng. - Tọa độ con trỏ hoặc đối tượng được
chọn. - Đơn vị chiều dài. - Đơn vị tần số. - Đơn vị thời gian. - Đơn vị
góc. - Chế độ chọn và chế độ hiển thị. - Trạng thái solver hoặc tác vụ
hiện tại.

Đơn vị phải được quản lý thống nhất trong toàn dự án và có thể thay đổi
từ Project Settings.

## 4. Các phân hệ chức năng của phần mềm

Không xây dựng phần mềm như một màn hình 3D đơn lẻ. Hãy tổ chức thành
các module có giao diện và trách nhiệm độc lập:

### 4.1. Project Management

-   Tạo, mở, lưu và đóng dự án.
-   Quản lý cấu hình dự án.
-   Quản lý đường dẫn tài nguyên.
-   Lưu lịch sử thao tác nếu phù hợp.
-   Kiểm tra tính toàn vẹn của dữ liệu dự án.

### 4.2. 3D Geometry Editor

-   Tạo primitive 3D.
-   Chỉnh sửa hình học.
-   Thực hiện phép toán Boolean.
-   Quản lý hệ tọa độ.
-   Gán vật liệu.
-   Quản lý cây đối tượng.
-   Hỗ trợ import/export định dạng CAD phổ biến theo khả năng thư viện
    được tích hợp.

### 4.3. Material Manager

-   Khai báo vật liệu điện môi.
-   Khai báo vật liệu dẫn điện.
-   Định nghĩa độ dẫn điện, hằng số điện môi, độ từ thẩm và tổn hao.
-   Hỗ trợ vật liệu phụ thuộc tần số nếu solver cho phép.
-   Có thư viện vật liệu và khả năng tạo vật liệu tùy chỉnh.

### 4.4. Simulation Setup

-   Chọn loại bài toán.
-   Chọn solver phù hợp.
-   Thiết lập dải tần.
-   Khai báo nguồn và ports.
-   Thiết lập điều kiện biên.
-   Cấu hình mesh.
-   Khai báo field monitors.
-   Kiểm tra mô hình trước khi chạy.

### 4.5. Electromagnetic Solver

Thiết kế lớp giao tiếp solver độc lập với giao diện. Các phương pháp có
thể được phát triển theo từng giai đoạn: - FEM -- Finite Element
Method. - FDTD -- Finite-Difference Time-Domain. - FIT -- Finite
Integration Technique. - MoM -- Method of Moments.

Không giả định rằng tất cả solver phải được triển khai ngay từ đầu. Hãy
xây dựng kiến trúc plugin/adapter để có thể bổ sung solver về sau.

### 4.6. Circuit Editor và Circuit Solver

-   Xây dựng sơ đồ mạch.
-   Quản lý linh kiện và kết nối.
-   Phân tích mạch.
-   Liên kết cổng mạch với mô hình EM.
-   Hỗ trợ co-simulation trong giai đoạn phát triển phù hợp.

### 4.7. Post-Processing

Các kết quả cần hướng tới: - S11, S21 và các S-parameters. - Smith
Chart. - VSWR. - Input Impedance. - Gain và Directivity. - Radiation
Pattern. - E-Field và H-Field. - Surface Current. - Power Density và
Poynting Vector. - Near-field/Far-field. - Bảng dữ liệu và xuất kết quả.

Chỉ hiển thị loại kết quả mà solver thực sự cung cấp.

## 5. Yêu cầu UI/UX

-   Giao diện kỹ thuật, rõ ràng, ưu tiên mật độ thông tin hợp lý.
-   Có thể sử dụng giao diện sáng và tối.
-   Các panel có thể dock, undock, resize và reset layout.
-   Có tab cho nhiều dự án hoặc nhiều kết quả.
-   Có tooltip cho các công cụ.
-   Có menu chuột phải phù hợp ngữ cảnh.
-   Có trạng thái lựa chọn rõ ràng.
-   Các thao tác thường dùng phải dễ truy cập.
-   Không lạm dụng màu sắc; màu cần có ý nghĩa nhất quán.
-   Các bảng phải hỗ trợ sắp xếp, lọc và chỉnh sửa khi phù hợp.
-   Không để các nút giao diện chỉ mang tính trang trí; mỗi nút phải có
    hành vi xác định hoặc được đánh dấu chưa khả dụng.

## 6. Yêu cầu đa nền tảng và kiến trúc kỹ thuật

Phần mềm phải hướng đến Linux và macOS. Không phụ thuộc vào API chỉ có
trên Windows.

Kiến trúc khuyến nghị: - Tách UI khỏi xử lý hình học và solver. - Dùng
mô hình plugin/adapter cho solver. - Tách Project Model, Geometry Model,
Simulation Model và Result Model. - Giao tiếp giữa các module thông qua
API nội bộ rõ ràng. - Có cơ chế lưu/đọc dự án ổn định. - Có logging và
xử lý lỗi tập trung. - Các tác vụ mô phỏng nặng không được làm treo UI;
sử dụng worker thread/process phù hợp. - Có kiểm thử cho các module quan
trọng.

Có thể đánh giá các công nghệ như Qt, OpenCascade, VTK, OpenGL/Vulkan và
các thư viện số học phù hợp. Việc lựa chọn công nghệ phải dựa trên khả
năng tương thích giấy phép, tính ổn định, hỗ trợ Linux/macOS và khả năng
tích hợp lâu dài.

## 7. Yêu cầu về thiết kế và triển khai theo giai đoạn

Không cố gắng triển khai toàn bộ CST ngay trong phiên bản đầu tiên.

**Giai đoạn 1 -- Giao diện và khung ứng dụng** - Tạo Main Window. - Xây
dựng Menu Bar và Ribbon. - Xây dựng Navigation Tree. - Tạo 3D
Viewport. - Tạo Schematic View ở mức giao diện. - Tạo Parameter List. -
Tạo Progress/Messages. - Hỗ trợ dock/resize và lưu layout.

**Giai đoạn 2 -- Mô hình hình học** - Tạo primitive 3D. - Chọn và chỉnh
sửa đối tượng. - Boolean operations. - Material Manager. - Parameterized
Geometry.

**Giai đoạn 3 -- Chuẩn bị mô phỏng** - Simulation Setup. - Ports và
boundary conditions. - Mesh configuration. - Solver interface.

**Giai đoạn 4 -- Solver và kết quả** - Tích hợp solver đầu tiên. - Quản
lý job mô phỏng. - Hiển thị 1D/2D/3D results. - Field plot và color
scale. - Xuất dữ liệu.

**Giai đoạn 5 -- Tính năng nâng cao** - Circuit Editor. -
Co-simulation. - Parameter Sweep. - Optimization. - Scripting và
automation.

## 8. Kết quả đầu ra mong muốn

Trước khi triển khai, hãy cung cấp:

1.  Phân tích bố cục giao diện và sơ đồ phân cấp chức năng.
2.  Sơ đồ kiến trúc module.
3.  Danh sách các cửa sổ, panel, tab và component.
4.  Mô tả trách nhiệm và dữ liệu đầu vào/đầu ra của từng module.
5.  Đề xuất công nghệ phù hợp với Linux và macOS, kèm lý do lựa chọn.
6.  Kế hoạch triển khai theo giai đoạn và thứ tự ưu tiên.

Nếu được yêu cầu viết mã nguồn: - Tạo cấu trúc thư mục rõ ràng. - Tách
UI, business logic, data model và solver backend. - Không viết toàn bộ
ứng dụng trong một file. - Cung cấp mã nguồn có thể biên dịch/chạy
được. - Ghi rõ thư viện phụ thuộc và các bước build trên Linux/macOS. -
Không tạo dữ liệu mô phỏng giả dưới danh nghĩa kết quả thực. - Ưu tiên
hoàn thiện một luồng chức năng nhỏ nhưng hoạt động thực tế trước khi mở
rộng.

## 9. Nguyên tắc quan trọng

Mục tiêu là xây dựng một nền tảng mô phỏng điện từ chuyên nghiệp, có khả
năng phát triển lâu dài, chứ không chỉ tạo một giao diện trông giống
CST.

Hãy giữ sự tương đồng ở cấp độ **cách tổ chức công việc và nhóm chức
năng**: quản lý dự án, mô hình 3D, thiết lập mô phỏng, sơ đồ mạch, tham
số, kết quả và hậu xử lý. Thiết kế nhận diện, biểu tượng, bố cục chi
tiết và tài nguyên đồ họa cần có bản sắc riêng.

Khi có điểm chưa rõ, hãy nêu giả định và đề xuất phương án trước khi
triển khai; không tự ý tạo ra các chức năng kỹ thuật mà backend chưa hỗ
trợ.
