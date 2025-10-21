<?php
// set header untuk content-type JSON
header('Content-Type: application/json');
// data array yang akan diubah menjadi JSON
$data = array("name" => "Wawan Sismadi");
// encode array menjadi JSON dan tampilkan
echo json_encode($data);
?>
