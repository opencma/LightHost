file(TO_NATIVE_PATH "${APP}" native_app)
string(REPLACE "\\" "\\\\" native_app "${native_app}")
file(WRITE "${OUTPUT}" "101 RCDATA \"${native_app}\"\n")
