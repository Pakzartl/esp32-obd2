import 'package:flutter_test/flutter_test.dart';
import 'package:adv350_app/main.dart';

void main() {
  testWidgets('App launches', (tester) async {
    await tester.pumpWidget(const Adv350App());
    expect(find.text('Scan for ADV350'), findsOneWidget);
  });
}
